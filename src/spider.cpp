#include <stdio.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include "spider.h"
#include "threads.h"
#include "qstring.h"
#include "thpool.h"
#include "url.h"
 
int g_epfd;
Config *g_conf;
extern int g_cur_thread_num;

static int set_nofile(rlim_t limit);
static void daemonize();
static void stat(int sig);
static int set_ticker(int second);

static void version()
{
    printf("Version: spider 1.0\n");
    exit(1);
}

static void usage()
{
    printf("Usage: ./spider [Options]\n"
            "\nOptions:\n"
            "  -h\t: this help\n"
            "  -v\t: print spiderq's version\n"
            "  -d\t: run program as a daemon process\n\n");
    exit(1);
}

int main(int argc, char *argv[]) 
{
    struct epoll_event events[10];
    int daemonized = 0;
    char ch;

    /* 解析命令行参数 */
    while ((ch = getopt(argc, (char* const*)argv, "vhd")) != -1) {
        switch(ch) {
            case 'v':
                version();
                break;
            case 'd':
                daemonized = 1;
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* 解析日志 */
    g_conf = initconfig();
    loadconfig(g_conf);

    /* s设置 fd num to 1024 */
    set_nofile(1024); 

    /* 载入处理模块 */
    vector<char *>::iterator it = g_conf->modules.begin();
    for(; it != g_conf->modules.end(); it++) {
        dso_load(g_conf->module_path, *it); 
    } 
	
	SPIDER_LOG(SPIDER_LEVEL_INFO,"Module import complete!");

    /* 添加爬虫种子 */
    if (g_conf->seeds == NULL) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "We have no seeds!");
    } else {
        int c = 0;
        char ** splits = strsplit(g_conf->seeds, ',', &c, 0);
        while (c--) {
            Surl * surl = (Surl *)malloc(sizeof(Surl));
            surl->url = url_normalized(strdup(splits[c]));
            surl->level = 0;
            surl->type = TYPE_HTML;
            if (surl->url != NULL){
                SPIDER_LOG(SPIDER_LEVEL_INFO,"input url: %s", surl->url);
				push_surlqueue(surl);
			}
        }
    }	

    /* 守护进程模式 */
    if (daemonized)
        daemonize();

    /* 设定下载路径 */
    if( chdir("download") )
		SPIDER_LOG(SPIDER_LEVEL_INFO,"Change to \"Download\" failed");

    /* begin create epoll to run */
    g_epfd = epoll_create(1024);



    /* 启动用于解析DNS的线程 */
    int err = -1;
    if ((err = create_thread(urlparser, NULL, NULL, NULL)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "Create urlparser thread fail: %s", strerror(err));
    }

	threadpool thpool = thpool_init(g_conf->max_job_num);


    /* waiting seed ourl ready */
    int try_num = 1;
    while(try_num < 8 && is_ipmap_empty())
        usleep((50000 << try_num++));

    if (try_num >= 8) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "NO ourl! DNS parse error?");
    }

    /* set ticker  */
    if (g_conf->stat_interval > 0) {
        signal(SIGALRM, stat);
        set_ticker(g_conf->stat_interval);
    }


	//TODO : 事件循环用单机的就行，解析用多线程，取消ourlqueue的锁

    /* epoll wait */
    int n, i;
    while(1) {
        n = epoll_wait(g_epfd, events, 10, 2000);
        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Come %d events", n);
        
        if (n == -1)
        {
			SPIDER_LOG(SPIDER_LEVEL_WARN, "epoll_wait system call failed!");
			exit(-1);
		}
        if (n <= 0) 
        {
			bool is_im_empty = is_ipmap_empty();
			bool is_surl_empty = is_surlqueue_empty();

            if (g_cur_thread_num <= 0 && is_im_empty && is_surl_empty ) {
				SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread : 0, ourl : 0, surl : 0");
                sleep(10);
				
                if (g_cur_thread_num <= 0 && is_ipmap_empty() && is_surlqueue_empty())
                    break;

            }else if(  g_cur_thread_num <=0 && is_im_empty && !is_surl_empty){
				SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Waiting ourl queue become avaliable, surl_queue size : %d", get_surl_queue_size());
			
			}else{
				SPIDER_LOG(SPIDER_LEVEL_DEBUG, "No event avaliable");
			}
        }

        for (i = 0; i < n; i++) {
            Ip_entry * arg = (Ip_entry *)(events[i].data.ptr);
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) 
			{
                SPIDER_LOG(SPIDER_LEVEL_WARN, "epoll fail, close socket %d",arg->sockfd);
				free_ip_entry(arg);
				arg = NULL;
                continue;
            }

            epoll_ctl(g_epfd, EPOLL_CTL_DEL, arg->sockfd, &events[i]); /* del event */
			
            //create_thread(recv_response, arg, NULL, NULL);
			thpool_add_work(thpool, recv_response, arg);
        }
    }

    SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Task done!");
    close(g_epfd);
    return 0;
}



static int set_nofile(rlim_t limit)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "getrlimit fail");
        return -1;
    }
    if (limit > rl.rlim_max) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "limit should NOT be greater than %lu", rl.rlim_max);
        return -1;
    }
    rl.rlim_cur = limit;
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "setrlimit fail");
        return -1;
    }
    return 0;
}

/**/
static void daemonize()
{
    int fd;
    if (fork() != 0) exit(0);
    setsid();
    SPIDER_LOG(SPIDER_LEVEL_INFO, "Daemonized...pid=%d", (int)getpid());	

    /* redirect stdin|stdout|stderr to /dev/null */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }

    /* redirect stdout to logfile */
    if (g_conf->logfile != NULL && (fd = open(g_conf->logfile, O_RDWR | O_APPEND | O_CREAT, 0)) != -1) {
        dup2(fd, STDOUT_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }

}

static int set_ticker(int second)
{
    struct itimerval itimer;
    itimer.it_interval.tv_sec = (long)second;
    itimer.it_interval.tv_usec = 0;
    itimer.it_value.tv_sec = (long)second;
    itimer.it_value.tv_usec = 0;

    return setitimer(ITIMER_REAL, &itimer, NULL);
}

static void stat(int sig)
{
    SPIDER_LOG(SPIDER_LEVEL_DEBUG, 
            "cur_thread_num=%d\tsurl_num=%d\tourl_num=%d",
            g_cur_thread_num,
            get_surl_queue_size(),
            get_ourl_entry_size());
}
