#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "uthread_inner.h"

int 
_sched_create(size_t stack_size) {           
    struct uthread_sched *new_sched;
    if ((new_sched = calloc(1, sizeof(struct uthread_sched))) == NULL) {
        perror("Failed to initialize scheduler\n");
        return errno;
    }
    assert(pthread_setspecific(uthread_sched_key, new_sched) == 0); // (重要，别漏了)

    new_sched->stack_size = (stack_size == 0) ? STACK_SIZE : stack_size;
    TAILQ_INIT(&new_sched->ready);
    return 0;
}

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
    _sched_free(sched);
}

