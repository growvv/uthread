#include <stdio.h>
#include "string.h"
#include "../timer.h"
#include "../uthread.h"
void handler(){
    _uthread_yield();
}

void a (void *x) {
    struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigaddset(&act.sa_mask,SIGALRM);
	pthread_sigmask(SIG_BLOCK,&act.sa_mask,NULL);
    add_timer(10,pthread_self());
    for (;;){}
}

void b (void *x) {
    struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigaddset(&act.sa_mask,SIGALRM);
	pthread_sigmask(SIG_BLOCK,&act.sa_mask,NULL);
//    add_timer(10,pthread_self());
    printf("b is running");
}

int main (int argc, char **argv) {
    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    
    for(;;);
    printf("main is running...\n");
    printf("main is exiting...\n");
    uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}