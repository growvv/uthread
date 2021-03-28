#ifndef TIMER_H
#define TIMER_H

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "uthread_inner.h"

#define TIME_WHEEL_SIZE 10

struct timer_node{
    struct timer_node *next;
    int rotation;
    struct uthread *ut;
};

struct timer_wheel {
    struct timer_node *slot[TIME_WHEEL_SIZE];
    int current;
};

void* create_timewheel(void* arg);
void tick(int signo);
void add_timer(int len, struct uthread *ut);

extern pthread_t global_pid;

#endif