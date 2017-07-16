#include "threads.h"
#include "spider.h"
#include "confparser.h"

 
/* 当前线程数 */
int g_cur_thread_num = 0;

/* 给当前线程数g_cur_thread_num的值上锁 */
pthread_mutex_t gctn_lock = PTHREAD_MUTEX_INITIALIZER;

int create_thread(void *(*start_func)(void *), void * arg, pthread_t *pid, pthread_attr_t * pattr)
{
	pthread_mutex_lock(&gctn_lock);
	g_cur_thread_num++;
	pthread_mutex_unlock(&gctn_lock);

    pthread_attr_t attr;
    pthread_t pt;

    if (pattr == NULL) {
        pattr = &attr;
        pthread_attr_init(pattr);
        pthread_attr_setstacksize(pattr, 1024*1024);
        pthread_attr_setdetachstate(pattr, PTHREAD_CREATE_DETACHED);
    }

    if (pid == NULL)
        pid = &pt;

    int rv = pthread_create(pid, pattr, start_func, arg);
    pthread_attr_destroy(pattr);
    return rv;
}

void begin_thread()
{
    SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Dispath to thread %lu", pthread_self());
}

void end_thread(Ip_entry* ip_entry)
{

	if( ip_entry != NULL ){

		string ip = ip_entry->ip_;
		Url* url = NULL;
		if((url=pop_ourlqueue( ip )) == NULL){
			SPIDER_LOG(SPIDER_LEVEL_INFO, "Crawl this ip over!: %s", ip.c_str());
			free_ip_entry(ip_entry);
			ip_entry = NULL;

		}
		else{
			add_epoll_task(ip_entry,url);
		}
	}
}
