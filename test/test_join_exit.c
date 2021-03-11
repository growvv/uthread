/* 对uthread_exit, uthread_join, uthread_self的测试  
*/

#include <stdio.h>
#include<unistd.h>

#include "uthread.h"

void 
a (void *x) {
    printf("a-ut id: %ld .\n", uthread_self());
    printf("a is running.\n");
    printf("a about to sleep for 2s.\n");
    sleep(2);
    printf("a is exiting\n");
    uthread_exit(NULL);
}

int main() {

    printf("main is running.\n");

    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    printf("main-ut id: %ld .\n", uthread_self());

    printf("main about to join a.\n");
    uthread_join(ut, NULL);
    printf("main waken up.\n");

    printf("main is existing.\n\n");
    uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}