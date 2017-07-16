#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include "url.h"
#include "socket.h"
#include "threads.h"
#include "qstring.h"
#include "dso.h"
 
/* regex pattern for parsing href */
static const char * HREF_PATTERN = "href=\"\\s*\\([^ >\"]*\\)\\s*\"";   
/* convert header string to Header object */
static Header * parse_header(char *header);

/* call modules to check header */
static int header_postcheck(Header *header);

int build_connect(int *fd, char *ip, int port)
{
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(struct sockaddr_in));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (!inet_aton(ip, &(server_addr.sin_addr))) {
        return -1;
    }

    if ((*fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    if (connect(*fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) < 0) {
        close(*fd);
        return -1;
    }

    return 0;
}

int send_request(int fd, void *arg)
{
    int need, begin, n;
    char request[1024] = {0};
    Url *url = (Url *)arg;

    sprintf(request, "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Accept: */*\r\n"
            "Connection: Keep-Alive\r\n"
            "User-Agent: Mozilla/5.0 (compatible; Qteqpidspider/1.0;)\r\n"
            "Referer: %s\r\n\r\n", url->path, url->domain, url->domain);

    need = strlen(request);
    begin = 0;
    while(need) {
        n = write(fd, request+begin, need);
        if (n <= 0) {
            if (errno == EAGAIN) { //write buffer full, delay retry
                usleep(1000);
                continue;
            }
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Thread %lu send ERROR: %d", pthread_self(), n);
            free_url(url);
            close(fd);
            return -1;
        }
        begin += n;
        need -= n;
    }
    return 0;
}

void set_nonblocking(int fd)
{
    int flag;
    if ((flag = fcntl(fd, F_GETFL)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "fcntl getfl fail");
    }
    flag |= O_NONBLOCK;
    if ((flag = fcntl(fd, F_SETFL, flag)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "fcntl setfl fail");
    }
}

#define HTML_MAXLEN   1024*1024

void  recv_response(void * arg)		//发送请求后返回，创建线程调用这个函数做后续处理
{
    begin_thread();		//打印一行日志

    int i, n,  len = 0;
    char * body_ptr = NULL;
    Ip_entry * narg = (Ip_entry *)arg;
    bool is_fault = false;
	bool is_filtered = false;
	bool is_chuncked = false;

	Response *resp = (Response *)malloc(sizeof(Response));
    resp->header = NULL;
    resp->body = (char *)malloc(HTML_MAXLEN);	//body动态分配
    resp->body_len = 0;
    resp->url = narg->nowurl;



	//正则表达式，提取url
    regex_t re;
    if (regcomp(&re, HREF_PATTERN, 0) != 0) {/* compile error */	
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "compile regex error");
    }

    //SPIDER_LOG(SPIDER_LEVEL_INFO, "Crawling url: %s/%s", narg->nowurl->domain, narg->nowurl->path);

    while(1) {
        /* what if content-length exceeds HTML_MAXLEN? */
        n = read(narg->sockfd, resp->body + len, 1024);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { 
                /**
                 * TODO: Why always recv EAGAIN?
                 * should we deal EINTR
                 */
                //SPIDER_LOG(SPIDER_LEVEL_WARN, "thread %lu meet EAGAIN or EWOULDBLOCK, sleep", pthread_self());
                usleep(100000);
                continue;
            } 
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Read socket fail: %s", strerror(errno));
            break;

        } else if (n == 0) {
            /* finish reading */
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Connection closed by peer: %s", narg->ip_.c_str());
			is_fault = true;

			break;
        } else {
            //SPIDER_LOG(SPIDER_LEVEL_WARN, "read socket ok! len=%d", n);
			//如果读完了header，则后面读到的就直接加在body上
			len += n;
            resp->body[len] = '\0';
			//printf("%s\n",resp->body);
			//fflush(stdout);
			//sleep(2);
	
            if (resp->header == NULL) 
			{
                if ((body_ptr = strstr(resp->body, "\r\n\r\n")) == NULL) 
				{	
					continue;
				}

				*(body_ptr+2) = '\0';
				resp->header = parse_header(resp->body);	//提取header
				
				if (!header_postcheck(resp->header))		//调用对应module: 
				{	
					is_filtered = true;
				}

				if( resp->header->transfer_encoding != NULL 
					&& (strncmp( resp->header->transfer_encoding, "chunked", 7) == 0)){
					is_chuncked = true;
				}
				

				/* cover header */
				body_ptr += 4;		
				for (i = 0; *body_ptr; i++) {
					resp->body[i] = *body_ptr;
					body_ptr++;
				}
				resp->body[i] = '\0';
				len = i;
                 
            }
	
				
			//is the body over？if not : continue else: complete read from socket
			if( is_chuncked ){
				if ( strstr(resp->body, "0\r\n") == NULL){
					continue;
				}

			}else if( resp->header->content_len!= 0 &&  resp->header->content_len > len ){
				continue;
			}else if( !is_chuncked && resp->header->content_len==0  ){		        
				SPIDER_LOG(SPIDER_LEVEL_WARN, "Html header connot figure out : %s:%s",narg->ip_.c_str(),narg->nowurl->domain );
				is_fault = true;
				break;
			}else{
				//normal case, just execute below
			}
				
			//body is over!
			SPIDER_LOG(SPIDER_LEVEL_DEBUG, "read this page over!: %s/%s",narg->nowurl->domain,narg->nowurl->path);

			if( is_filtered ){
				SPIDER_LOG(SPIDER_LEVEL_DEBUG, "this page has been filtered: %s",narg->nowurl->domain);
				break;
			}

			resp->body_len = len;

			if (resp->body_len > 0) {
				extract_url(&re, resp->body, narg->nowurl);
			}
			/* deal resp->body */
			for (i = 0; i < (int)modules_post_html.size(); i++) {
				modules_post_html[i]->handle(resp);		//调用模块
			}

			break;

        }/*if (n < 0) else if (n==0) else*/

    } /* while(1) */
	
	//善后处理
    free_url(narg->nowurl); /* free Url object */
	if ( is_fault  )
	{
		//TODO : 区分异常和read返回0的错误
		close(narg->sockfd); /* close socket */
		free_ip_entry(narg);
		narg = NULL;
	}
	
 
    regfree(&re); /* free regex object */
    /* free resp */
    free(resp->header->content_type);
	free(resp->header->transfer_encoding);
	free(resp->header->location);
    free(resp->header);
    free(resp->body);
    free(resp);

	//crawl new url
    end_thread(narg);
}


static int header_postcheck(Header *header)
{
    unsigned int i;
    for (i = 0; i < modules_post_header.size(); i++) {
        if (modules_post_header[i]->handle(header) != MODULE_OK)
            return 0;
    }
    return 1;
}

static Header * parse_header(char *header)
{
	//SPIDER_LOG(SPIDER_LEVEL_DEBUG, "%s",header);

    int c = 0;
    char *p = NULL;
    char **sps = NULL;
    char *start = header;
    Header *h = (Header *)calloc(1, sizeof(Header));
	h->content_type = NULL;
    h->transfer_encoding = NULL;
	h->location = NULL;


    if ((p = strstr(start, "\r\n")) != NULL) {
        *p = '\0';
        sps = strsplit(start, ' ', &c, 2);
        if (c == 3) {
            h->status_code = atoi(sps[1]);
        } else {
            h->status_code = 600; 
        }
        start = p + 2;
    }

    while ((p = strstr(start, "\r\n")) != NULL) {
        *p = '\0';
        sps = strsplit(start, ':', &c, 1);
        if (c == 2) {
            if (strcasecmp(sps[0], "content-type") == 0) {
                h->content_type = strdup(strim(sps[1]));
            }else if( strcasecmp( sps[0],"content-length") == 0 ){
				h->content_len = atoi(strim(sps[1]));
			}else if ( strcasecmp( sps[0],"transfer-encoding") == 0 )
			{
				h->transfer_encoding = strdup( strim( sps[1] ) );
			}else if( strcasecmp( sps[0],"location") == 0 ){
				h->location = strdup( strim( sps[1] ) );
			}else{
				//other header parse 
			}
        }
	
        start = p + 2;
    }
    return h;
}
