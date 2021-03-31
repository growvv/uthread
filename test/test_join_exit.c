/* 对uthread_exit, uthread_join, uthread_self等协程管理接口的测试 */

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "uthread.h"

void *
a () {
    printf("a-ut id: %ld .\n", pthread_self());
    printf("a is running.\n");
    pthread_exit(NULL);
}

void *
b () {
    printf("b-ut id: %ld .\n", pthread_self());
    printf("b is running.\n");
    printf("b about to sleep for 2s.\n");
    sleep(2);
    printf("b is exiting\n");
}

int main() {
    enable_hook();
    
    printf("main is running.\n");
    printf("main-ut id: %ld .\n", pthread_self());

    pthread_t p, p2;
    pthread_create(&p, NULL, a, NULL);
    pthread_create(&p2, NULL, b, NULL);

    printf("main about to join a.\n");
    pthread_join(p, NULL);
    printf("main waken up.\n");

    printf("main about to join b.\n");
    pthread_join(p2, NULL);
    printf("main waken up.\n");

    printf("main is existing.\n\n");
    
    main_end();   
}