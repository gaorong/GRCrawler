#ifndef QURL_H
#define QURL_H
 
#include <event.h>
#include <evdns.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex.h>
#include <queue>
#include <map>
#include <stdlib.h>
#include <sys/epoll.h>
#include <string>
#include "spider.h"
#include "bloomfilter.h"
using namespace std;


#define MAX_LINK_LEN 128

#define TYPE_HTML  0
#define TYPE_IMAGE 1

typedef struct Surl {
    char  *url;
    int    level;
    int    type; 
} Surl;

typedef struct Url {
    char *domain;
    char *path;
    int  port;
    char *ip;
    int  level;
} Url;

typedef struct evso_arg {
    int     fd;
    Url     *url;
} evso_arg;

class Ip_entry
{
public:
	int sockfd;
	Url * nowurl;
	string ip_;
	queue<Url *>* ourl_queue;	

	Ip_entry(Url *url): sockfd(-1),nowurl(url) {
	
		if( nowurl == NULL ){
			//SPIDER_LOG(SPIDER_LEVEL_WARN , "Initial Ip_entry with empty Url");
			exit(-1);
		}

		ip_ = string(nowurl->ip);
		ourl_queue = new queue<Url *>;
	}
};

typedef map<string, Ip_entry* > IpMap;

extern void push_surlqueue(Surl * url);
extern Url * pop_ourlqueue(string ip);
extern void push_ourlqueue(Url * ourl);
extern void push_map( string ip, Ip_entry* new_ip_entry );
extern int is_ipmap_empty();
extern int is_surlqueue_empty();

extern void * urlparser(void * arg);
extern char * url2fn(const Url * url);
extern char * url_normalized(char *url);
extern void free_url(Url * ourl);
extern int is_ipmap_empty();
extern void free_ip_entry( Ip_entry* entry );

extern int get_surl_queue_size();
extern int get_ourl_queue_size();
extern int extract_url(regex_t *re, char *str, Url *domain);
extern int iscrawled(char * url);
extern char * attach_domain(char *url, const char *domain);
extern void free_ip_entry( Ip_entry* entry );
extern void add_epoll_task(Ip_entry* ip_entry  ,Url* ourl);
extern int get_ourl_entry_size();



#endif
