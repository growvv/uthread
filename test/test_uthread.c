#include <stdio.h>

#include "uthread.h"
#include "myhook.h"
#include <pthread.h>

void 
a (void *x) {
    for (int i = 0; i < 100; i = i + 2) {
        printf("thread a: %d\n", i);
        _uthread_yield();
    }
}

void 
b (void *x) {
    for (int i = 1; i < 100; i = i + 2) {
        printf("thread b: %d\n", i);
        _uthread_yield();
    }
}

void 
c (void *x) {
    for (int i = 1; i < 100; i = i + 2) {
        printf("thread c: %d\n", i);
        _uthread_yield();
    }
}

int
main (int argc, char **argv) {

    // struct uthread *ut = NULL;
    // uthread_create(&ut, a, NULL);
    // struct uthread *ut2 = NULL;
    // uthread_create(&ut2, b, NULL);
    // struct uthread *ut3 = NULL;
    // uthread_create(&ut3, c, NULL);
    // printf("main is running...\n");
    // printf("main is exiting...\n");
    // uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
    enable_hook();
    pthread_t p1,p2,p3;//unsigned long int
    // create
    pthread_create(&p1,NULL,a,NULL);
    pthread_create(&p2,NULL,a,NULL);
    pthread_create(&p3,NULL,a,NULL);

    printf("main is running...\n");
    printf("main is exiting...\n");
    // end
    uthread_main_end();
}