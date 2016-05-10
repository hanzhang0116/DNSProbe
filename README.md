## DNSProbe

This program periodically sends DNS queries to the nameservers of domains (e.g., Top 100 Alexa domains) and stores the latency values in a MySQL table. The frequency of queries and the file including domains can be specified by the user on command line. To make sure the query does not hit the DNS cache while trying to query for a site, a random string is always prepended to each domain. For example, to measure performance to google.com, send a query to <random>.google.com (e.g., 1234xpto.google.com). Besides the time series values, some statistics about each domain are also kept in the MySQL database, including Average query time, Standard deviation of query times, Number of queries made so far, Timestamp of first and last query made.

Next step work is to continouslly run this program to probe the top 100 Alaxa domains and provide results in JSON on a website. For example, [{"domain": "baidu.com", "stdTime": 1328.8, "avgTime": 607.202, "lastQueryTime": "2016-05-09 23:25:49", "queryNumber": 4055, "firstQueryTime": "2016-05-08 00:34:43"}]

1. Dependencies:
	MySQL, MySQL++, ldns, ldns-devel, boost, boost-devel

2. MySQL scheme:
  * Table "domains" is used to keep track the probed domain and the statistics. The scheme is as follows: 
    * domain VARCHAR(64) NOT NULL PRIMARY KEY, 
    * avgTime Float , 
    * stdTime FLOAT, 
    * queryNumber INT default 0, 
    * firstQueryTime TIMESTAMP not null DEFAULT CURRENT_TIMESTAMP, 
    * lastQueryTime TIMESTAMP not null DEFAULT CURRENT_TIMESTAMP

  * Table "delays" is used to keep track individual probe results. The scheme is as follows: 
    * probe_id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY, 
    * domain VARCHAR(64) NOT NULL, 
    * delay FLOAT NOT NULL, 
    * time TIMESTAMP NOT NULL

2. Usage:
Options:
	* -f, specify the frequency in seconds to probe the domains
	* -d, specify the file that includes the domains to probe

3. Compile and Run Examples:
  Compile: 
    g++ -std=c++11 -o DNSProbe DNSProbe.cpp -lldns -lmysqlpp -I/usr/local/include/mysql++ -I/usr/local/include/mysql/ -lmysqlclient -lm -lboost_system -lboost_date_time
  Run:
    ./DNSProbeWork -f 300 -d domainFile
