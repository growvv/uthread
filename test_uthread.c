#include <stdio.h>

#include "uthread_inner.h"
#include "uthread.h"

void 
a (void *x) {
    printf("a is running...\n");
    printf("a is yielding...\n");
    _uthread_yield(); 
    printf("a is running...\n");
    printf("a is yielding...\n");
    _uthread_yield(); 
    printf("a is running...\n");
    printf("a is exiting...\n");
    // 测试用
    // printf("is state exited? %d\n", (_sched_get()->current_uthread->state & BIT(UT_ST_EXITED)) != 0);     
}

void 
b (void *x) {
    printf("b is running...\n");
    printf("b is yielding...\n");
    _uthread_yield(); 
    printf("b is running...\n");
    printf("b is yielding...\n");
    _uthread_yield(); 
    printf("b is running...\n");
    printf("b is exiting...\n");
    // 测试用
    // printf("is state exited? %d\n", (_sched_get()->current_uthread->state & BIT(UT_ST_EXITED)) != 0);     
}

int
main (int argc, char **argv) {
    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    _uthread_yield();   

    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    
    printf("main is running...\n");
    printf("main is yielding...\n");   
    _uthread_yield();    

    _uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}