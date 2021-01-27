#include <stdio.h>

#include "uthread_inner.h"
#include "uthread.h"

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

int
main (int argc, char **argv) {

    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    
    _uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}