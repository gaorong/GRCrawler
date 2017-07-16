#ifndef QTHREADS_H
#define QTHREADS_H

class Ip_entry;
#include "url.h"
#include <pthread.h>



extern  int create_thread(void *(*start_routine) (void *), void *arg, pthread_t * thread, pthread_attr_t * pAttr);
extern  void begin_thread();
extern  void end_thread(Ip_entry* ip_entry);

#endif
