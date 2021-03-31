#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "uthread.h"

// 测试协程在死循环中被抢占的情况
void *
a (void *x) {
    for (;;){
        printf("a is running\n");
        sleep(1000);
    } 
}

void *
b (void *x) {
    for (;;) {
        printf("b is running\n");
        sleep(1000);
    }
}

// 测试协程执行完毕后让出的情况
void * 
c (void *x) {
    printf("c is running\n");
}


int main (int argc, char **argv) {
    enable_hook();

    pthread_t p1,p2,p3;
    pthread_create(&p1,NULL, a, NULL);
    pthread_create(&p2,NULL, b, NULL);
    pthread_create(&p3,NULL, c, NULL);
    
    for(;;) {
        printf("main is running...\n");
        sleep(1);
    }
    
    main_end();   
}