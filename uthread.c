#include <stdlib.h>     // free
#include <assert.h>     // assert
#include <pthread.h>
#include <unistd.h>     // getpagesize
#include <stdio.h>      // perror
#include <errno.h>      // errno

#include "uthread_inner.h"

int _switch(struct context *new_ctx, struct context *cur_ctx);
#ifdef __i386__
__asm__ (
"    .text                                  \n"
"    .p2align 2,,3                          \n"
".globl _switch                             \n"
"_switch:                                   \n"
"__switch:                                  \n"
"movl 8(%esp), %edx      # fs->%edx         \n"
"movl %esp, 0(%edx)      # save esp         \n"
"movl %ebp, 4(%edx)      # save ebp         \n"
"movl (%esp), %eax       # save eip         \n"
"movl %eax, 8(%edx)                         \n"
"movl %ebx, 12(%edx)     # save ebx,esi,edi \n"
"movl %esi, 16(%edx)                        \n"
"movl %edi, 20(%edx)                        \n"
"movl 4(%esp), %edx      # ts->%edx         \n"
"movl 20(%edx), %edi     # restore ebx,esi,edi      \n"
"movl 16(%edx), %esi                                \n"
"movl 12(%edx), %ebx                                \n"
"movl 0(%edx), %esp      # restore esp              \n"
"movl 4(%edx), %ebp      # restore ebp              \n"
"movl 8(%edx), %eax      # restore eip              \n"
"movl %eax, (%esp)                                  \n"
"ret                                                \n"
);
#elif defined(__x86_64__)

__asm__ (
"    .text                                  \n"
"       .p2align 4,,15                                   \n"
".globl _switch                                          \n"
".globl __switch                                         \n"
"_switch:                                                \n"
"__switch:                                               \n"
"       movq %rsp, 0(%rsi)      # save stack_pointer     \n"
"       movq %rbp, 8(%rsi)      # save frame_pointer     \n"
"       movq (%rsp), %rax       # save insn_pointer      \n"
"       movq %rax, 16(%rsi)                              \n"
"       movq %rbx, 24(%rsi)     # save rbx,r12-r15       \n"
"       movq %r12, 32(%rsi)                              \n"
"       movq %r13, 40(%rsi)                              \n"
"       movq %r14, 48(%rsi)                              \n"
"       movq %r15, 56(%rsi)                              \n"
"       movq 56(%rdi), %r15                              \n"
"       movq 48(%rdi), %r14                              \n"
"       movq 40(%rdi), %r13     # restore rbx,r12-r15    \n"
"       movq 32(%rdi), %r12                              \n"
"       movq 24(%rdi), %rbx                              \n"
"       movq 8(%rdi), %rbp      # restore frame_pointer  \n"
"       movq 0(%rdi), %rsp      # restore stack_pointer  \n"
"       movq 16(%rdi), %rax     # restore insn_pointer   \n"
"       movq %rax, (%rsp)                                \n"
"       ret                                              \n"
);
#endif

pthread_key_t uthread_sched_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void 
_uthread_key_destructor(void *data) {
    free(data);     
}

/* 被pthread_once绑定的函数，每个线程只执行一次，用于线程内部的协程共用一个调度器 */
static void 
_uthread_key_create(void) {
    assert(pthread_key_create(&uthread_sched_key, _uthread_key_destructor) == 0);
    assert(pthread_setspecific(uthread_sched_key, NULL) == 0);
}

int 
uthread_create(struct uthread **new_ut, void *func, void *arg) {    // (缺attr参数)
    struct uthread *ut = NULL;
    assert(pthread_once(&key_once, _uthread_key_create) == 0);
    struct uthread_sched *sched = _sched_get();
    if (sched == NULL) {
        _sched_create(0);   // 若参数为零，则使用既定的STACK_SIZE
        sched = _sched_get();
        if (sched == NULL) {
            perror("Failed to create scheduler");
            return (-1);
        }
    }

    if ((ut = calloc(1, sizeof(struct uthread))) == NULL) {
        perror("Failed to allocate memory for new uthread");
        return errno;
    }
    if (posix_memalign(&ut->stack, getpagesize(), sched->stack_size)) {
        free(ut);
        perror("Failed to allocate stack for new uthread");   
        return errno;
    }
    ut->sched = sched;
    ut->stack_size = sched->stack_size;
    ut->state = BIT(UT_ST_NEW);
    ut->func = func;
    ut->arg = arg;
    *new_ut = ut;
    
    TAILQ_INSERT_TAIL(&ut->sched->ready, ut, ready_next);
    // if (TAILQ_EMPTY(&ut->sched->ready))
    //     perror("Failed to insert!");
    return 0;
}

void
_uthread_yield(struct uthread *ut) {
    TAILQ_INSERT_TAIL(&ut->sched->ready, ut, ready_next);
    _switch(&ut->sched->ctx, &ut->ctx);
}

// 【待理解】
static void
_uthread_exec(void *ut)
{
    ((struct uthread *)ut)->func(((struct uthread *)ut)->arg);
    ((struct uthread *)ut)->state |= BIT(UT_ST_EXITED);

    _uthread_yield(ut);
}

// 【待理解】
static void
_uthread_init(struct uthread *ut)
{
    void **stack = NULL;
    stack = (void **)(ut->stack + (ut->stack_size));

    stack[-3] = NULL;
    stack[-2] = (void *)ut;
    ut->ctx.esp = (void *)stack - (4 * sizeof(void *));
    ut->ctx.ebp = (void *)stack - (3 * sizeof(void *));
    ut->ctx.eip = (void *)_uthread_exec;
    ut->state = BIT(UT_ST_READY);
}

static void 
_uthread_free(struct uthread *ut) {
    free(ut->stack);
    free(ut);
}

int
_uthread_resume(struct uthread *ut) {
    struct uthread_sched *sched = _sched_get();
    
    if (ut->state & BIT(UT_ST_NEW)) 
        _uthread_init(ut);
    
    sched->current_uthread = ut;
    _switch(&ut->ctx, &ut->sched->ctx);
    sched->current_uthread = NULL;

    if (ut->state & BIT(UT_ST_EXITED)) {
        TAILQ_REMOVE(&ut->sched->ready, ut, ready_next);
        _uthread_free(ut);
    }

    return 0;
}



