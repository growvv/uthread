#include <stdio.h>

#include "uthread_inner.h"
#include "uthread.h"

void 
a (void *x) {
    struct uthread *ut = _sched_get()->current_uthread; 
    printf("a is running\n");
    printf("a is yielding\n");
    _uthread_yield(ut); 
    printf("a is running\n");
    printf("a is yielding\n");
    _uthread_yield(ut); 
    printf("a is running\n");
    printf("a is yielding\n");
}

void 
b (void *x) {
    struct uthread *ut = _sched_get()->current_uthread; 
    printf("b is running\n");
    printf("b is yielding\n");
    _uthread_yield(ut); 
    printf("b is running\n");
    printf("b is yielding\n");
    _uthread_yield(ut); 
    printf("b is running\n");
    printf("b is yielding\n");     
}

int
main (int argc, char **argv) {
    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    _sched_run();
}