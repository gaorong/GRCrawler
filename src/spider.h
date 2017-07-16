#ifndef SPIDER_H
#define SPIDER_H
 
#include <stdarg.h>
#include <vector>

#include "socket.h"
#include "threads.h"
#include "confparser.h"
#include "dso.h"

/* macros */
#define MAX_MESG_LEN   1024

#define SPIDER_LEVEL_DEBUG 0
#define SPIDER_LEVEL_INFO  1
#define SPIDER_LEVEL_WARN  2
#define SPIDER_LEVEL_ERROR 3
#define SPIDER_LEVEL_CRIT  4 

static const char * LOG_STR[] = { 
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRIT"
};

extern Config *g_conf;

#define SPIDER_LOG(level, format, ...) do{ \
    if (level >= g_conf->log_level) {\
        time_t now = time(NULL); \
        char msg[MAX_MESG_LEN]; \
        char buf[32]; \
        sprintf(msg, format, ##__VA_ARGS__); \
        strftime(buf, sizeof(buf), "%Y%m%d %H:%M:%S", localtime(&now)); \
        fprintf(stdout, "[%s] [%s] [%s(%d)-%s] [%lu] : %s\n", buf, LOG_STR[level], __FILE__, __LINE__,__FUNCTION__, pthread_self() ,msg); \
        fflush(stdout); \
    } \
    if (level == SPIDER_LEVEL_ERROR) {\
        exit(-1); \
    } \
} while(0)


//宏的后面是不需要加 ';'的 ，调用宏的时候加上就可以了
//##__VA_ARGS__  表示  ## 表示连接两个字符串，__VA_ARGS__表示将宏中的可变参数依次展开
//strftime是time.h中的函数，表示将时间格式写入到一个字符串中

extern int attach_epoll_task();
extern int g_epfd;

#endif
