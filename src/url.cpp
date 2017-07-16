#include "url.h"
#include "dso.h"

static queue <Surl *> surl_queue;
 
//static queue<Url *> ourl_queue;

static IpMap ip_map;

static map<string, string> host_ip_map;

static Url * surl2ourl(Surl *url);
static void dns_callback(int result, char type, int count, int ttl, void *addresses, void *arg);
static int is_bin_url(char *url);
static int surl_precheck(Surl *surl);
//static void get_timespec(timespec * ts, int millisecond);

pthread_mutex_t oq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  oq_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t  sq_cond = PTHREAD_COND_INITIALIZER;

void push_surlqueue(Surl *url)
{
    if (url != NULL && surl_precheck(url)) {
        pthread_mutex_lock(&sq_lock);  
        surl_queue.push(url);
        if (surl_queue.size() == 1)
            pthread_cond_signal(&sq_cond);
        pthread_mutex_unlock(&sq_lock);
    }
}

//TODO : if the parameter of ip is NULL, return an new urlEntry
Url * pop_ourlqueue(string ip)
{
    Url *url = NULL;
    pthread_mutex_lock(&oq_lock);
	
	//SPIDER_LOG(SPIDER_LEVEL_DEBUG,"ourl queue`s size is %d",ourl_queue.size());
	IpMap::iterator iter = ip_map.find(ip);
	if( iter == ip_map.end() ){
		SPIDER_LOG(SPIDER_LEVEL_DEBUG, "canot find this ip in ipmap : %s", ip.c_str());
		return NULL;
	}
	
	queue<Url *>* q = iter->second->ourl_queue;    
	if (! q->empty() ) {
        url = q->front();
        q->pop();
        pthread_mutex_unlock(&oq_lock);
        return url;
    } else {
		SPIDER_LOG(SPIDER_LEVEL_DEBUG, "ip: %s is fully empty", ip.c_str());
		// map delete here, but the memory of Ip_entry is not free!
		ip_map.erase(ip);
        pthread_mutex_unlock(&oq_lock);
        return url;
    }
}

void push_ourlqueue(Url * ourl)
{
	pthread_mutex_lock(&oq_lock);
	queue<Url *>* q = NULL;

	string ip( ourl->ip );
	IpMap::iterator iter = ip_map.find(ip);
	if( iter == ip_map.end() ){
	    pthread_mutex_unlock(&oq_lock);

		SPIDER_LOG(SPIDER_LEVEL_DEBUG, "canot find this ip in ipmap : %s", ip.c_str());
		Ip_entry* new_ip_entry = new Ip_entry(ourl);
		add_epoll_task(new_ip_entry,ourl);
		push_map(string(ip), new_ip_entry);
		return;
	}else{
		q = iter->second->ourl_queue;  
	}
	
    q->push(ourl);
    pthread_mutex_unlock(&oq_lock);
}



void push_map( string ip, Ip_entry* new_ip_entry ){
	//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "push_map");
	pthread_mutex_lock(&oq_lock);
	ip_map[ip] = new_ip_entry;
    pthread_mutex_unlock(&oq_lock);
}

static int surl_precheck(Surl *surl)
{
    unsigned int i;
    for (i = 0; i < modules_pre_surl.size(); i++) {
        if (modules_pre_surl[i]->handle(surl) != MODULE_OK)
            return 0;
    }
    return 1;
}



int is_ipmap_empty() 
{
	//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "is_ipmap_empty");
    pthread_mutex_lock(&oq_lock);
    int val = ip_map.empty();
    pthread_mutex_unlock(&oq_lock);
    return val;
}

int is_surlqueue_empty() 
{
    pthread_mutex_lock(&sq_lock);
    int val = surl_queue.empty();
    pthread_mutex_unlock(&sq_lock);
    return val;
}


void free_ip_entry( Ip_entry* entry ){

	//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "free_ip_entry");

	pthread_mutex_lock(&oq_lock);
	string ip = entry->ip_ ;
	ip_map.erase( ip );
	if( entry->sockfd != -1 )
		close(entry->sockfd); /* close socket */
	
	while(!entry->ourl_queue->empty())
    {
		Url* url = entry->ourl_queue->front();
		entry->ourl_queue->pop();
		free_url(url); /* free Url object */	
	}
	delete entry->ourl_queue;

	//delete entry;
	//entry = NULL;
    pthread_mutex_unlock(&oq_lock);
	SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Destructe the entry of %s", ip.c_str());
}


void add_epoll_task(Ip_entry* ip_entry  ,Url* ourl){

	//if comes new ip, establish a new entry and add in epoll
	struct epoll_event ev;
	int sock_rv;		//返回值
	
	ip_entry->nowurl = ourl;
	if( ip_entry->sockfd == -1 ){
		/* connect socket and get sockfd */
		if ((sock_rv = build_connect(&ip_entry->sockfd, ourl->ip, ourl->port)) < 0) {
			SPIDER_LOG(SPIDER_LEVEL_WARN, "Build socket connect fail: %s", ourl->ip);
			exit(-1);
		}

		set_nonblocking(ip_entry->sockfd);
	}
	
	//TODO : Judge the socket if still alive!
	if ((sock_rv = send_request(ip_entry->sockfd, ourl)) < 0) {
		SPIDER_LOG(SPIDER_LEVEL_WARN, "Send socket request fail: %s", ourl->ip);
		exit(-1);
	} 

	ev.data.ptr = ip_entry;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, ip_entry->sockfd, &ev) == 0) {/* add event */
		SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Attach an epoll event success! ：%s : %s/%s",ip_entry->ip_.c_str(),ourl->domain,ourl->path);
	} else {
		SPIDER_LOG(SPIDER_LEVEL_WARN, "Attach an epoll event fail!");
		exit(-1);
	}


}

void * urlparser(void *none)
{
    SPIDER_LOG(SPIDER_LEVEL_INFO, "dns parser thread start!");

    Surl *url = NULL;
    Url  *ourl = NULL;
    map<string, string>::const_iterator itr;
    //event_base * base = event_base_new();
    //evdns_base * dnsbase = evdns_base_new(base, 1);
    //event_base_loop(base,EVLOOP_NONBLOCK);

    while(1) {
        pthread_mutex_lock(&sq_lock);
        while (surl_queue.empty()) {
            pthread_cond_wait(&sq_cond, &sq_lock);
        }
        url = surl_queue.front();
        surl_queue.pop();
        pthread_mutex_unlock(&sq_lock);

        ourl = surl2ourl(url);
        //SPIDER_LOG(SPIDER_LEVEL_DEBUG, "parser the ip of : %s", url->url);

        
        itr = host_ip_map.find(ourl->domain);	//首先查找map
        if (itr == host_ip_map.end()) { /* not found */
            /* dns resolve */
			//TODO : 是否可以复用这个event,能不能不用每次都初始化
            event_base * base = event_init();
            evdns_init();
	        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Dns resolve begin parser: %s", url->url);

            evdns_resolve_ipv4(ourl->domain, 0, dns_callback, ourl);
            event_dispatch();
            event_base_free(base);

            //evdns_base_resolve_ipv4(dnsbase, ourl->domain, 0, dns_callback, ourl);
            //event_base_loop(base, EVLOOP_ONCE | EVLOOP_NONBLOCK);

        } else {
			//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "find this ip in dns cache queue : %s : %s ", ourl->domain, ourl->ip);

            ourl->ip = strdup(itr->second.c_str());
			push_ourlqueue( ourl );
        }
    }

    //evdns_base_free(dnsbase, 0);
    //event_base_free(base);
    return NULL;
}

/*
 * 返回最后找到的链接的下一个下标,如果没找到返回 0;
 */
int extract_url(regex_t *re, char *str, Url *ourl)
{
    const size_t nmatch = 2;
    regmatch_t matchptr[nmatch];
    int len;
	SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Begin extrac url %s/%s",ourl->domain,ourl->path);

    char *p = str;
    while (regexec(re, p, nmatch, matchptr, 0) != REG_NOMATCH) {
        len = (matchptr[1].rm_eo - matchptr[1].rm_so);
        p = p + matchptr[1].rm_so;
        char *tmp = (char *)calloc(len+1, 1);
        strncpy(tmp, p, len);
        tmp[len] = '\0';
        p = p + len + (matchptr[0].rm_eo - matchptr[1].rm_eo);

        /* exclude binary file */
        if (is_bin_url(tmp)) {
            free(tmp);
            continue;
        }


        char *url = attach_domain(tmp, ourl->domain);
        if (url != NULL) {
            //SPIDER_LOG(SPIDER_LEVEL_DEBUG, "extra an url: %s", url);
            Surl * surl = (Surl *)malloc(sizeof(Surl));
            surl->level = ourl->level + 1;
            surl->type = TYPE_HTML;

            /* normalize url */
            if ((surl->url = url_normalized(url)) == NULL) {
                SPIDER_LOG(SPIDER_LEVEL_WARN, "Normalize url fail");
                free(surl);
                continue;
            }

            if (iscrawled(surl->url)) { 
                //SPIDER_LOG(SPIDER_LEVEL_DEBUG, "this url has scrawed!: %s", surl->url);
                free(surl->url);
                free(surl);
                continue;
            } else {
                push_surlqueue(surl);
            }
        }
    }

    return (p-str);
}

/* if url refer to binary file
 * image: jpg|jpeg|gif|png|ico|bmp
 * flash: swf
 */
//static char * BIN_SUFFIXES = ".jpg.jpeg.gif.png.ico.bmp.swf";
static char const * BIN_SUFFIXES = ".js.aspx.css.xml";
static int is_bin_url(char *url)
{

    char *p = NULL;
	int i = 0;
	

    if ((p = strrchr(url, '.')) != NULL) {
		
		char postfix[5] = {0};
		while( *p != '?' && i < 5 && *p != '\0'){
			postfix[i++] = *p++;
		}

        if (strstr(BIN_SUFFIXES, postfix) == NULL){
			//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "%s",url);

            return 0;
		}
		else{

            return 1;
		}
    }
    return 0;
}

char * attach_domain(char *url, const char *domain)
{
    if (url == NULL)
        return NULL;

    if (strncmp(url, "http", 4) == 0) {
        return url;

    } else if (*url == '/') {
        int i;
        int ulen = strlen(url);
        int dlen = strlen(domain);
        char *tmp = (char *)malloc(ulen+dlen+1);
        for (i = 0; i < dlen; i++)
            tmp[i] = domain[i];
        for (i = 0; i < ulen; i++)
            tmp[i+dlen] = url[i];
        tmp[ulen+dlen] = '\0';
        free(url);
        return tmp;

    } else {
        //do nothing
        free(url);
        return NULL;
    }
}

char * url2fn(const Url * url)
{
    int i = 0;
    int l1 = strlen(url->domain);
    int l2 = strlen(url->path);
    char *fn = (char *)malloc(l1+l2+2);

    for (i = 0; i < l1; i++)
        fn[i] = url->domain[i];

    fn[l1++] = '_';

    for (i = 0; i < l2; i++)
        fn[l1+i] = (url->path[i] == '/' ? '_' : url->path[i]);

    fn[l1+l2] = '\0';

    return fn;
}

int iscrawled(char * url) {
    return search(url); /* use bloom filter algorithm */
}

static void dns_callback(int result, char type, int count, int ttl, void *addresses, void *arg) 
{
    Url * ourl = (Url *)arg;
    struct in_addr *addrs = (in_addr *)addresses;

    if (result != DNS_ERR_NONE || count == 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "Dns resolve fail: %s", ourl->domain);
    } else {

        char * ip = inet_ntoa(addrs[0]);
        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Dns resolve OK: %s -> %s", ourl->domain, ip);
		host_ip_map[ourl->domain] = strdup(ip);

        ourl->ip = strdup(ip);
		Ip_entry* new_ip_entry = new Ip_entry(ourl);
		add_epoll_task(new_ip_entry,ourl);
		push_map(string(ip), new_ip_entry);
    }
    event_loopexit(NULL); // not safe for multithreads 
}

static Url * surl2ourl(Surl * surl)
{
    Url *ourl = (Url *)calloc(1, sizeof(Url));
    char *p = strchr(surl->url, '/');
    if (p == NULL) {
        ourl->domain = surl->url;
        ourl->path = surl->url + strlen(surl->url); 
    } else {
        *p = '\0';
        ourl->domain = surl->url;
        ourl->path = p+1;	
    }
    // port
    p = strrchr(ourl->domain, ':');
    if (p != NULL) {
        *p = '\0';
        ourl->port = atoi(p+1);
        if (ourl->port == 0)
            ourl->port = 80;

    } else {
        ourl->port = 80;
    }
    // level
    ourl->level = surl->level;
    return ourl;
}

char * url_normalized(char *url) 
{
    if (url == NULL) return NULL;

    /* rtrim url */
    int len = strlen(url);
    while (len && isspace(url[len-1]))
        len--;
    url[len] = '\0';

    if (len == 0) {
        free(url);
        return NULL;
    }

    /* remove http(s):// */
    if (len > 7 && strncmp(url, "http", 4) == 0) {
        int vlen = 7;
        if (url[4] == 's') /* https */
            vlen++;

        len -= vlen;
        char *tmp = (char *)malloc(len+1);
        strncpy(tmp, url+vlen, len);
        tmp[len] = '\0';
        free(url);
        url = tmp;
    }

    /* remove '/' at end of url if have */
    if (url[len-1] == '/') {
        url[--len] = '\0';
    }

    if (len > MAX_LINK_LEN) {
        free(url);
        return NULL;
    }

    return url;
}


void free_url(Url * ourl)
{
    free(ourl->domain);
    //free(ourl->path);
    free(ourl->ip);
    free(ourl);
}

int get_surl_queue_size()
{
    return surl_queue.size();
}

//int get_ourl_queue_size()
//{
//	pthread_mutex_lock(&oq_lock);
//	int qs = ourl_queue.size();
//	pthread_mutex_lock(&oq_lock);
//
//}


int get_ourl_entry_size(){
	pthread_mutex_lock(&oq_lock);
    IpMap::iterator iter;
	int es = 0;
	
	for(iter = ip_map.begin(); iter!=ip_map.end(); iter++)
	{
		es += (iter->second)->ourl_queue->size();
	}
	
	pthread_mutex_unlock(&oq_lock);
	return es;
}