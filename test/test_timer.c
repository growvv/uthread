#include <stdio.h>
#include "string.h"
#include "../timer.h"
#include "../uthread.h"

void handler_alias(){
    printf("收到了指示执行yield的信号\n");
    // add_timer(2, _sched_get()->cur_uthread);
    sigset_t set;
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    _uthread_yield();
}

void a (void *x) {
    for (;;){
        printf("a is running\n");
        sleep(1000);
        // printf("aaaaaaaaaaaaa\n");
    } 
}

void b (void *x) {
    for (;;) {
        printf("b is running\n");
        sleep(1000);
        // printf("bbbbbbbbbbbbbb\n");
    }
}

// void c (void *x) {
//     printf("c is running\n");
//     for (;;){
//         sleep(1000);
//         printf("cccccccccccc\n");
//     }
// }


int main (int argc, char **argv) {
    // pthread_create(&global_pid, NULL, thread_for_test, NULL);
    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    // struct uthread *ut3 = NULL; 
    // uthread_create(&ut3, c, NULL);
    // uthread_join(ut,NULL);
    // uthread_join(ut2,NULL);
    // uthread_join(ut3,NULL);
    
    for(;;) {
        printf("main is running...\n");
        sleep(1000);
        // printf("main_main_main\n");
    }
    
    // printf("end");
    uthread_main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}