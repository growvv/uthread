/* 对uthread_exit, uthread_join, uthread_self的测试  
*/

#include <stdio.h>
#include <unistd.h>

#include "uthread.h"
#include <pthread.h>
#include "myhook.h"
void *a () {
    printf("a-ut id: %ld .\n", pthread_self());
    printf("a is running.\n");
    // pthread_exit(NULL);
    printf("a about to sleep for 2s.\n");
    sleep(2);
    printf("a is exiting\n");
    
}

int main() {
    printf("main is running.\n");
    enable_hook();
    
    pthread_t p;
    pthread_create(&p, NULL, a,NULL);
    printf("main-ut id: %ld .\n", pthread_self());

    printf("main about to join a.\n");
    pthread_join(p, NULL);
    printf("main waken up.\n");

    printf("main is existing.\n\n");
    uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
    return 0;
}