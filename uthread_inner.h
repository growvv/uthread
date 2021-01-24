#ifndef UTHREAD_INNER_H
#define UTHREAD_INNER_H

#include <time.h>           //  size_t
#include <pthread.h>
#include <sys/queue.h>      // for queue.h

#define BIT(x) (1 << (x))
#define CLEARBIT(x) ~(1 << (x))

#define STACK_SIZE (128*1024) /* 128k */

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
    UT_ST_NEW,                          // 已创建但还未初始化，需要在resume之前进行初始化
    UT_ST_READY,                        // 初始化后即可进入就绪状态
    UT_ST_EXITED,                       // 已退出，等待清除
};

struct uthread {
    struct context          ctx;
    void                    *stack;
    size_t                  stack_size;
    uthread_func            func;
    void                    *arg;      // 传递给func的参数
    enum uthread_st         state;                  
    struct uthread_sched    *sched;
    TAILQ_ENTRY(uthread)    ready_next;     // 用于在sched的uthread队列中提供前后指针
};

// 声明结构体：带尾指针的uthread队列。结构体的名字为uthread_que
// 之后可通过struct uthread_que来定义一个队列  
TAILQ_HEAD(uthread_que, uthread); 

struct uthread_sched {
    struct context          ctx;
    void                    *stack;     // 【啥时候用到了？】
    size_t                  stack_size;
    struct uthread          *current_uthread;  
    struct uthread_que      ready;
};

extern pthread_key_t uthread_sched_key;

static struct uthread_sched* // 【没有static会出错】
_uthread_get_sched() {
    return pthread_getspecific(uthread_sched_key);
}

int _sched_create(size_t stack_size);
int _sched_run();

void _uthread_yield(struct uthread *ut);
int _uthread_resume(struct uthread *ut);

#endif