#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "timer.h"
// #include "uthread_inner.h"

void* create_timewheel(void* data){
    signal(SIGALRM, tick);
    struct itimerval new_value, old_value;
    new_value.it_value.tv_sec = 0;
    new_value.it_value.tv_usec = 1;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 1000;
    setitimer(ITIMER_REAL, &new_value, &old_value);
}

void tick(int signo)
{
    struct timer_node **cur = &timer.slot[timer.current];
    while (*cur) {
        struct timer_node *curr = *cur;
        if (curr->rotation > 0) {
            curr->rotation--;
            cur = &curr->next;
        } else {
            pthread_kill(curr->ut->p->tid, SIGQUIT);
            *cur = curr->next;    
            free(curr);
        }
    }
    timer.current = (timer.current + 1) % TIME_WHEEL_SIZE;
    printf("count");
}

void add_timer(int len, struct uthread *ut)
{
    int pos = (len + timer.current) % TIME_WHEEL_SIZE;
    struct timer_node *node = (struct timer_node*)malloc(sizeof(struct timer_node));

    // 插入到对应格子的链表头部即可, O(1)复杂度
    node->next = timer.slot[pos];
    timer.slot[pos] = node;
    node->rotation = len / TIME_WHEEL_SIZE;
    node->ut = ut;
}