#include <stdio.h>
#include "string.h"
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

#define TIME_WHEEL_SIZE 10
struct timer_node{
    struct timer_node *next;
    int rotation;
    pthread_t ut;
};

struct timer_wheel {
    struct timer_node *slot[TIME_WHEEL_SIZE];
    int current;
};

static struct timer_wheel timer = {{0}, 0};
void tick(int signo)
{
    printf("tick\n");
    struct timer_node **cur = &timer.slot[timer.current];
    while (*cur) {
        struct timer_node *curr = *cur;
        if (curr->rotation > 0) {
            curr->rotation--;
            cur = &curr->next;
        } else {
            pthread_kill(curr->ut, SIGQUIT);
            *cur = curr->next;    
            free(curr);
        }
    }
    timer.current = (timer.current + 1) % TIME_WHEEL_SIZE;
}

void add_timer(int len, pthread_t ut)
{
    int pos = (len + timer.current) % TIME_WHEEL_SIZE;
    struct timer_node *node = (struct timer_node*)malloc(sizeof(struct timer_node));

    // 插入到对应格子的链表头部即可, O(1)复杂度
    node->next = timer.slot[pos];
    timer.slot[pos] = node;
    node->rotation = len / TIME_WHEEL_SIZE;
    node->ut = ut;
}
void* create_timewheel(void* arg){
    printf("create timewheel successfully\n");
    signal(SIGALRM, tick);
    struct itimerval new_value, old_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_usec = 0;
    new_value.it_interval.tv_sec = 1;
    new_value.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &new_value, &old_value);
    for(;;){
        sleep(1000);
    }       
    printf("thread exit\n");
}

void handler(){
    printf("handle\n");
    add_timer(2, pthread_self());
}

void handler2() {
    printf("测试线程被唤醒了一次");
}

void* thread_for_test(void* arg){
    struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigaddset(&act.sa_mask,SIGALRM);
	pthread_sigmask(SIG_BLOCK,&act.sa_mask,NULL);
    act.sa_handler = handler2;
    sigaction(SIGQUIT,&act,NULL);
    for (;;){
        sleep(1000);
    }
}

int main (int argc, char **argv) {
    pthread_t ut;
    pthread_create(&ut,NULL,create_timewheel, NULL);
    struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigaddset(&act.sa_mask,SIGALRM);
	pthread_sigmask(SIG_BLOCK,&act.sa_mask,NULL);
    act.sa_handler = handler;
    sigaction(SIGQUIT,&act,NULL);
    add_timer(2, pthread_self());
    for(;;);
}