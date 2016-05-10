
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <mysql++.h>
#include <vector>
#include <cmath>
#include <numeric>
#include <ldns/ldns.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>

using namespace std;

namespace probe{

class DNSProbe{

	private:
		char db[128];
		char host[128];
		char user[128];
		char passwd[128];
		int frequency;
		list<string> domainList;

public:
DNSProbe(string dbStr, string hostStr, string userStr, string passwdStr, int val, list<string> domains)
{
	frequency = val;
	strcpy(db, dbStr.c_str());
	strcpy(host, hostStr.c_str());
	strcpy(user, userStr.c_str());
	strcpy(passwd, passwdStr.c_str());
	domainList = domains;
}
  
public:
void run(){
	boost::asio::io_service io;
	boost::asio::deadline_timer t(io, boost::posix_time::seconds(0));
	t.async_wait(boost::bind(&DNSProbe::setTimer, this, boost::asio::placeholders::error, &t, &domainList, &frequency));
	io.run();
}

private:
void probeDomainList(list<string> * domainList){
	// Connect to the database
	mysqlpp::Connection conn;
	try{
		//conn.set_option(new mysqlpp::ReconnectOption(true));
		//set 60 seconds as timeout of the connection to DB
		conn.set_option(new mysqlpp::ConnectTimeoutOption(60));
		conn.connect(db, host, user, passwd);
  	}
	catch (exception& e) {
		cerr << "DB connection exception caught: " << e.what() << endl;
	}
	// create a new resolver from /etc/resolv.conf
	ldns_resolver *resolver;
	ldns_status status;
	status = ldns_resolver_new_frm_file(&resolver, NULL);
	if (status != LDNS_STATUS_OK) {
		cerr << "create resolver error" << endl;
	}
	list<string>::iterator it;
	cout << "Probe the domains" << endl;
	for (it=domainList->begin(); it!=domainList->end(); ++it){
	    //cout << *it << endl;
    	domainQuery(*it, resolver, conn);
	}
	ldns_resolver_deep_free(resolver);
}

private:
void setTimer(const boost::system::error_code& /*e*/,
      boost::asio::deadline_timer* t, list<string> * domainList, int * frequency)
{
	t->expires_at(t->expires_at() + boost::posix_time::seconds(*frequency));
	t->async_wait(boost::bind(&DNSProbe::setTimer, this, boost::asio::placeholders::error, t, domainList, frequency));
	probeDomainList(domainList);
}

private:
int insertDelay(int delay, string domain, mysqlpp::Connection conn){
	//insert probe delay into table delays
    string queryInsertDelay = "INSERT INTO delays VALUE (NULL, '" + domain + "', " + to_string(delay) + ", CURRENT_TIMESTAMP)";
	try{
		mysqlpp::Query query = conn.query(queryInsertDelay.c_str());
		mysqlpp::SimpleResult res = query.execute();
		if(res){
			//cout << "insert OK" << endl;
			return 0;
		}
		else{
			cerr << "Failed to query table delay: " << query.error() << endl;
			return -1;
		}
	}
	catch (exception& e){
		cerr << "Exception: " << e.what() << endl;
	}
    return 0;
}

private:
int updateTableDomains(int delay, string domain, mysqlpp::Connection conn){
	double avg=0;
	double stdDev=0;
	string queryExistStr = "select * from domains where domain='" + domain + "'";
	//cout << queryExistStr << endl;

	try{
		mysqlpp::Query query = conn.query(queryExistStr.c_str());
		mysqlpp::StoreQueryResult resExist = query.store();
		if(resExist){
			//the domain was not probed earlier
			if(resExist.num_rows()<1){
				//insert an row for the domain
				string queryInsertDomain = "INSERT INTO domains VALUE ('" + domain + "'," + to_string(delay) + ", 0, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)";
				//cout << queryInsertDomain << endl;
				query = conn.query(queryInsertDomain.c_str());
				mysqlpp::SimpleResult resInsertDomain = query.execute();
				if(resInsertDomain){
					if (resInsertDomain.rows()>0) {
						//cout << "insert OK" << endl;
						return 0;
					}
				}
				else {
					cerr << "Failed to insert table domains: " << query.error() << endl;
					return -1;
				}
			}
		}
		else {
			cerr << "Failed to query table domains: " << query.error() << endl;
			return -1;
		}
	}
	catch (exception& e){
		cerr << "Exception: " << e.what() << endl;
	}

	//calculate average and standard deviation of delays
	string queryDelaysStr = "select domain, delay from delays where domain='" + domain + "'";
	//cout << queryDelaysStr << endl;
	try{
		mysqlpp::Query query = conn.query(queryDelaysStr.c_str());
		mysqlpp::StoreQueryResult resDelays = query.store();
    
		if(resDelays){
			// Get each row in result set
			boost::accumulators::accumulator_set<double, boost::accumulators::features<boost::accumulators::tag::mean, boost::accumulators::tag::variance>> acc; 
			for (int i = 0; i < resDelays.num_rows(); ++i) {
		  		acc(double(resDelays[i]["delay"]));
			}
			avg=boost::accumulators::mean(acc);
			stdDev=sqrt(boost::accumulators::variance(acc));
		}
		else{
			cerr << "Failed to query table delays: " << query.error() << endl;
			return -1;
		}
	}
	catch (exception& e){
		cerr << "Exception: " << e.what() << endl;
	}
   
	//update avgTime, stdTime, queryNumber, lastQueryTime in table domains
	string queryUpdateDomain = "UPDATE domains SET avgTime=" + to_string(avg) + ", stdTime=" + to_string(stdDev) +  ", queryNumber = queryNumber+1, lastQueryTime = CURRENT_TIMESTAMP where domain='" + domain + "'";     
	//cout << queryUpdateDomain << endl;
	try{
		mysqlpp::Query query = conn.query(queryUpdateDomain.c_str());
		mysqlpp::SimpleResult resUpdateDomain = query.execute();
		if(resUpdateDomain){
			//cout << "update domain" << endl;
		}
		else{
			cerr << "Failed to update table domains: " << query.error() << endl;
			return -1;
		}
	}
	catch (exception& e){
		cerr << "Exception: " << e.what() << endl;
	}
	return 0;
}

private:
int updateDB(int delay, string domain, mysqlpp::Connection conn)
{
	if(insertDelay(delay, domain, conn)==-1)
	{
		return -1;
	}
	if(updateTableDomains(delay, domain, conn)==-1){
		return -1;
	}
	return 0;
}

private:
string random_string(int length)
{
	char subDomain[length];
	static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";
	srand(time(NULL));
	for (int i = 0; i < length; ++i) {
		subDomain[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	subDomain[length]=0;
	string subDomainStr(subDomain);
	//cout << subDomainStr << endl;
	return subDomain;
}

private:
int domainQuery(string domainToQuery, ldns_resolver *resolver, mysqlpp::Connection conn){
	ldns_rdf *domain;
	ldns_pkt *pkt;
	int randomLen = 10;
	string randomDomain = random_string(randomLen) + "." + domainToQuery;

	//cout << randomDomain << endl;
	domain = ldns_dname_new_frm_str(randomDomain.c_str());
	if(!domain)
	{
		cerr << "generate random domain error" << endl;
		return -1;
	}

	auto begin = chrono::high_resolution_clock::now();    
	pkt = ldns_resolver_query(resolver,
					domain,
					LDNS_RR_TYPE_A,
					LDNS_RR_CLASS_IN,
					LDNS_RD);

	if(!pkt){
		cerr << "create domain query packet error" << endl;
		return -1;
	}
	else{
		auto end = chrono::high_resolution_clock::now();    
		auto dur = end - begin;
		int ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
		updateDB(ms, domainToQuery, conn);
		ldns_pkt_free(pkt);
		/*
		//we do not care about the responses
		// retrieve the A records
		ldns_rr_list *records;
		records = ldns_pkt_rr_list_by_type(pkt,
						LDNS_RR_TYPE_A,
						LDNS_SECTION_ANSWER);
		if(!records){
		}
		else{
			ldns_rr_list_deep_free(records);
		}
		*/
	}  
	ldns_rdf_deep_free(domain);
	return 0;
}

}; // class DNSProbe
} // namespace probe

int main(int argc, char* argv[])
{
	int frequency = 0;
	int tmp;
	char domainFile[256];
	string db = "probes";
	string host = "localhost";
	string user = "root";
	string passwd = "test";

	if(argc<=2)
	{
		cout << "Usage: -f <frequency> -d <domainFile>" << endl;
		exit(0);
	}
	while((tmp=getopt(argc, argv, "d:f:"))!=-1)
	{
	    switch(tmp)
    	{
			case 'd':
				strcpy(domainFile, optarg);
				break;
			case 'f':
				frequency=atoi(optarg);
				break;
			default:
				exit(0);
		}
	}
	
	//cout << "Domain file is " << domainFile << endl;
	//cout << "The specified frequency is " << frequency << " seconds" << endl;

	//load domains from file
	string line;
	list<string> domains;
	ifstream infile(domainFile);
	while (getline(infile, line)){
		domains.push_back(line);
	}
	infile.close();

	cout << "We are probing the following domains every " << to_string(frequency) << " seconds" << endl;
	list<std::string>::iterator it;
	for (it=domains.begin(); it!=domains.end(); ++it){
	    cout << *it << endl;
	}

	//probe::DNSProbe probeDomains("probes", "localhost", "root", "test", frequency, domains);
	probe::DNSProbe probeDomains(db, host, user, passwd, frequency, domains);
	probeDomains.run();

	return 0;
}
