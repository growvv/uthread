#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "uthread_inner.h"

struct uthread_sched* 
_sched_get() {
    return pthread_getspecific(uthread_sched_key);
}

static int 
_sched_work_done(struct uthread_sched *sched) {
    return TAILQ_EMPTY(&sched->ready);
}

static void
_sched_free(struct uthread_sched *sched) {
    free(sched->stack);
    free(sched);
}

int 
_sched_run() {
    struct uthread_sched *sched = NULL;
    struct uthread *last_ready = NULL, *ut = NULL;

    sched = _sched_get();
    while (!_sched_work_done(sched)) {
        /* 执行就绪队列中的uthread */
        last_ready = TAILQ_LAST(&sched->ready, uthread_que);
        while (!TAILQ_EMPTY(&sched->ready)) {
            ut = TAILQ_FIRST(&sched->ready);
            TAILQ_REMOVE(&ut->sched->ready, ut, ready_next);
            _uthread_resume(ut);
            if (ut == last_ready)         
                break;
        }
    }

    printf("Congratulations, all uthreads done!\n");

    _sched_free(sched);    
    exit(0);    // 代替main函数的return语句结束整个进程
}

int 
_sched_create() {           
    struct uthread_sched *new_sched;
    if ((new_sched = calloc(1, sizeof(struct uthread_sched))) == NULL) {
        perror("Failed to initialize scheduler\n");
        return errno;
    }
    
    assert(pthread_setspecific(uthread_sched_key, new_sched) == 0); // (重要，别漏了)

    // if (posix_memalign(&new_sched->stack, getpagesize(), STACK_SIZE)) {    
    //     free(new_sched);
    //     perror("Failed to allocate stack for sched");   
    //     return errno;
    // }

    /* 【奇怪的问题】上面posix_memalign到最后free(sched->stack)的时候会出错，而改成下面calloc就不会*/
    if ((new_sched->stack = calloc(1, STACK_SIZE)) == NULL) {
        free(new_sched);
        perror("Failed to allocate stack for sched");
        return errno;
    }

    new_sched->stack_size = STACK_SIZE;
    TAILQ_INIT(&new_sched->ready);

    /* 为调度器初始化上下文，直接搬的_uthread_init中的代码 */
    void **stack = (void **)(new_sched->stack + (new_sched->stack_size));   
    stack[-3] = NULL;
    stack[-2] = (void *)new_sched;
    new_sched->ctx.esp = (void *)stack - (4 * sizeof(void *));     
    new_sched->ctx.ebp = (void *)stack - (3 * sizeof(void *)); 
    new_sched->ctx.eip = (void *)_sched_run;

    return 0;
}
