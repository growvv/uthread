#ifndef UTHREAD_H
#define UTHREAD_H

#include <time.h>   // for size_t

#include "queue.h"

struct context {    // 【目前是直接copy的，未思考哪些寄存器必须作为上下文】
    void     *esp;
    void     *ebp;
    void     *eip;
    void     *edi;
    void     *esi;
    void     *ebx;
    void     *r1;
    void     *r2;
    void     *r3;
    void     *r4;
    void     *r5;
};

typedef void (*uthread_func)(void *);
enum uthread_st {
    UT_ST_NEW,                          // 已创建但还未初始化
    UT_ST_READY,                        // 就绪
    UT_ST_EXITED,                       // 已退出，等待清除
};

struct uthread {
    struct context          ctx;
    void                    *stack;
    size_t                  stack_size;
    uthread_func            func;
    void                    *args;      // 传递给func的参数
    enum uthread_st         state;                  
    struct uthread_sched    *sched;
};

TAILQ_HEAD(uthread_que, uthread);       // 定义结构：带尾指针的uthread队列

struct uthread_sched {
    struct context          ctx;
    void                    *stack;
    size_t                  stack_size;
    struct uthread          *current_uthread;  
    struct uthread_que      ready;
};

#endif