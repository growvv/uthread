
// myhook.c
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include "myhook.h"
#include "uthread.h"



int pthread_create(pthread_t *tidp,const pthread_attr_t *attr,void *(*start_rtn)(void*),void *arg) {
    
    // int (*mypthread_create)(pthread_t *tidp,const pthread_attr_t *attr,void *(*start_rtn)(void*),void *arg) = dlsym(RTLD_NEXT, "pthread_create");

    struct uthread *ut = NULL;
    uthread_create(&ut,start_rtn,arg);
    *tidp = (unsigned long)ut;
    return 1;
}

int pthread_join(pthread_t thread, void **retval) {
    printf("in join \n");
    struct uthread* ut = thread;
    uthread_join(ut,NULL);
    int (*sys_pthread_join)(pthread_t thread, void **retval) = dlsym(RTLD_NEXT, "pthread_join");
    sys_pthread_join(thread,retval);
    return 0;
}


pthread_t pthread_self(void) {
    return uthread_self();
}


void pthread_exit(void *retval) {
    uthread_exit(retval);
}

ssize_t	 read(int filedes,void *buf,size_t nbytes) {
    ssize_t res = uthread_io_read(filedes,buf,nbytes);
    return res;
}


ssize_t write(int filed,const void *buf,size_t nbytes) {
    ssize_t res = uthread_io_write(filed,buf,nbytes);
    return res;
}

int enable_hook() {
    return 1;
}