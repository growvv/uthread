#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>

#include "timer.h"
#include "uthread_inner.h"

static struct timer_wheel timer;

void * 
create_timewheel(void* arg) {
    // printf("create timewheel successfully\n");
    timer.current = 0;

    /* 打印时间轮 */
    // printf("tick里的槽位情况: ");
    // int i = 0;
    // for (i = 0; i < 10; ++i) {
    //     if (timer.slot[i])
    //         printf("%ld: ", timer.slot[i]->ut->p->tid);
    // }

    signal(SIGALRM, tick);
    /* 时间轮时间间隔为10ms */
    struct itimerval new_value, old_value;
    new_value.it_value.tv_sec = 0;
    new_value.it_value.tv_usec = 10000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 10000;
    setitimer(ITIMER_REAL, &new_value, &old_value);

    // printf("时间轮初始化完毕\n");
    
    for(;;){
        sleep(1000);
    }       
}

void 
tick(int signo) {
    // printf("tick\n");
    struct timer_node **cur = &timer.slot[timer.current];
    // printf("current: %d\n", timer.current);
    while (*cur) {
        struct timer_node *curr = *cur;
        // printf("tick rotation: %d\n", curr->rotation);
        // printf("tick ut: %ld\n", curr->ut->id);
        if (curr->rotation > 0) {
            curr->rotation--;
            cur = &curr->next;
        } else {
            // printf("线程id： %ld\n", curr->ut->p->tid);

            if (curr->ut->is_wating_yield_signal == 1) {
                // printf("向主线程发出指示yield的信号\n");
                assert(pthread_kill(curr->ut->p->tid, SIGUSR1) == 0);
                // printf("after\n");
            }
            
            *cur = curr->next;   
            free(curr);
            curr = NULL;     // NOTE!!!
            // printf("free掉了\n");
        }
    }
    timer.current = (timer.current + 1) % TIME_WHEEL_SIZE;
}

void 
add_timer(int len, struct uthread *ut) {
    int pos = (len + timer.current) % TIME_WHEEL_SIZE;
    // printf("pos:%d\n", pos);
    struct timer_node *node = (struct timer_node*)malloc(sizeof(struct timer_node));

    // 插入到对应格子的链表头部即可, O(1)复杂度
    // 头插法
    node->rotation = len / TIME_WHEEL_SIZE;
    node->ut = ut;
    node->next = timer.slot[pos];
    timer.slot[pos] = node;
}