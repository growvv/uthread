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

// 创建main协程后直接运行调度器，则main函数剩余的代码（包括uthread_create中剩余的部分）会以协程形式继续运行
int
_uthread_create_main() {
    struct uthread * ut_main = NULL;
    if ((ut_main = calloc(1, sizeof(struct uthread))) == NULL) {
        perror("failed to allocate memory for main-uthread");
        return errno;
    }

    /* main协程无需设置多余的字段，使用进程的栈空间而非堆空间 */
    ut_main->is_main = 1;
    ut_main->sched = _sched_get();
    ut_main->state = BIT(UT_ST_READY);  // 此处为直接赋值，会同时清掉NEW状态

    TAILQ_INSERT_TAIL(&ut_main->sched->ready, ut_main, ready_next);
    _switch(&ut_main->sched->ctx, &ut_main->ctx);
}

int 
uthread_create(struct uthread **new_ut, void *func, void *arg) {    // (缺attr参数)
    struct uthread *ut = NULL;
    assert(pthread_once(&key_once, _uthread_key_create) == 0);
    struct uthread_sched *sched = _sched_get();
    if (sched == NULL) {
        // 第一次调用uthread_create会创建调度器
        _sched_create();   
        sched = _sched_get();
        if (sched == NULL) {
            perror("Failed to create scheduler");
            return (-1);
        }
        // 第一次调用uthread_create还需要为main函数创建一个uthread
        _uthread_create_main();
    }

    if ((ut = calloc(1, sizeof(struct uthread))) == NULL) {
        perror("Failed to allocate memory for new uthread");
        return errno;
    }
    if (posix_memalign(&ut->stack, getpagesize(), STACK_SIZE)) {    // 从堆上为协程分配栈空间
        free(ut);
        perror("Failed to allocate stack for new uthread");   
        return errno;
    }
    ut->sched = sched;
    ut->stack_size = STACK_SIZE;
    ut->state = BIT(UT_ST_NEW);
    ut->func = func;
    ut->arg = arg;
    ut->is_main = 0;
    *new_ut = ut;
    
    TAILQ_INSERT_TAIL(&ut->sched->ready, ut, ready_next);
    // if (TAILQ_EMPTY(&ut->sched->ready))
    //     perror("Failed to insert!");
    return 0;
}

void
_uthread_yield() {
    struct uthread *ut = _sched_get()->current_uthread;
    if ((ut->state & BIT(UT_ST_EXITED)) == 0)   // 若不是退出状态，还需要把ut放回ready队列
        TAILQ_INSERT_TAIL(&ut->sched->ready, ut, ready_next);
    _switch(&ut->sched->ctx, &ut->ctx);
}

// 协程需要执行的全部流程：1）绑定的函数；2）设置EXITED状态位；3）yield回到调度器
static void
_uthread_exec(void *ut)
{
    ((struct uthread *)ut)->func(((struct uthread *)ut)->arg);
    ((struct uthread *)ut)->state |= BIT(UT_ST_EXITED);

    _uthread_yield();   // 协程执行完后会通过yield回到调度器！
}

// 初始化uthread的上下文
static void
_uthread_init(struct uthread *ut)
{
    void **stack = (void **)(ut->stack + (ut->stack_size));    // stack是指针数组；另外，栈是从上到下的

    stack[-3] = NULL;
    stack[-2] = (void *)ut;
    ut->ctx.esp = (void *)stack - (4 * sizeof(void *));     // 栈的起始位置为stack下移4个void指针大小
    ut->ctx.ebp = (void *)stack - (3 * sizeof(void *));     // ebp存放函数调用的帧指针，【但这个初始值是否合理？】
    ut->ctx.eip = (void *)_uthread_exec;
    ut->state = BIT(UT_ST_READY);   // 注意这里时=而不是|=，直接把NEW状态也清除了
}

static void 
_uthread_free(struct uthread *ut) {
    free(ut->stack);
    free(ut);
}

static void 
_uthread_free_main(struct uthread *ut) {
    free(ut);   // main协程不用堆空间作为自己的栈空间，无需释放栈空间
}

int
_uthread_resume(struct uthread *ut) {
    struct uthread_sched *sched = _sched_get();
    
    if (ut->state & BIT(UT_ST_NEW)) 
        _uthread_init(ut);

    sched->current_uthread = ut;
    _switch(&ut->ctx, &ut->sched->ctx);
    
    sched->current_uthread = NULL;
    // printf("is state exited? %d\n", (ut->state & BIT(UT_ST_EXITED)) != 0);     
    if (ut->state & BIT(UT_ST_EXITED)) {
        ut->is_main ? _uthread_free_main(ut) : _uthread_free(ut);
    }

    return 0;
}

// 用于main函数的末尾，直接切换到调度器，因为没有清除UT_ST_EXITED位，
// 调度器会删除main协程（如果调度器任务全部执行完毕，会通过exit退出整个进程）
void 
_uthread_main_end() {
    struct uthread *ut_main = _sched_get()->current_uthread;
    ut_main->state |= BIT(UT_ST_EXITED);   // 这样调度器就会删掉main协程了
    printf("main is running...\n");
    printf("main is exiting...\n");
    _switch(&ut_main->sched->ctx, &ut_main->ctx);   
}



