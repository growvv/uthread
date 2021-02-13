#include <stdlib.h>     // free
#include <assert.h>     // assert
#include <pthread.h>
#include <unistd.h>     // getpagesize
#include <stdio.h>      // perror
#include <errno.h>      // errno
#include <sys/types.h>

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

/* 定义运行时的全局数据 */
struct global_data global_data;
struct sched all_sched[MAX_COUNT_SCHED];
struct p all_p[MAX_PROCS];

pthread_key_t uthread_sched_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;     // 与该变量绑定的函数每个线程只会执行一次

/* 作为线程特有数据的解构函数，用于在线程结束时释放key关联的地址处的资源。
 *（单一的sched不是通过malloc类函数创建的，直接令pthread_key_create不绑定此函数） 
 */
// static void 
// _uthread_key_destructor(void *data) {
//     // printf("here\n");
//     free((data);     
// }

/* 被pthread_once绑定的函数，只由某一个线程执行一次，之后其它线程不会再执行 */
static void    
_uthread_key_create(void) {
    assert(pthread_key_create(&uthread_sched_key, NULL) == 0);  // 第二个参数改为NULL，即不绑定_uthread_key_destructor
    assert(pthread_setspecific(uthread_sched_key, NULL) == 0);
}

/* 用于创建main thread */
int
_uthread_create_main() {
    struct uthread * ut_main = NULL;
    struct sched *sched = _sched_get();

    if ((ut_main = calloc(1, sizeof(struct uthread))) == NULL) {
        perror("failed to allocate memory for main-uthread");
        return errno;
    }
    /* main协程无需设置多余的字段，使用线程的栈空间而非堆空间，所以free的时候也不需要free(ut->stack) */
    ut_main->is_main = 1;
    ut_main->status = BIT(UT_ST_READY);  // 此处为直接赋值，会同时清掉NEW状态

    TAILQ_INSERT_TAIL(&sched->p->ready, ut_main, ready_next);
    sched->global->n_uthread++;         // 此处不用加锁，还没有别的线程

    /* 这一步很重要，它使得main函数中的代码得以尽可能早地以协程的身份运行。
     * 此时调度器已经创建并初始化完毕，可以直接切换到调度器，切换的同时保存了自己的上下文； 
     * 调度器进入调度循环后马上又会执行main协程，从而继续执行main函数之后的代码 */
    _switch(&sched->ctx, &ut_main->ctx); 

    return 0;
}

int
uthread_create(struct uthread **new_ut, void *func, void *arg) {
    struct uthread *ut = NULL;
    struct sched *sched = NULL; 
    
    /* 为线程特有数据创建一个新的key，以后各个线程就可以通过uthread_sched_key来访问自己的那一份数据了（用作读取sched）。
     * _uthread_key_create在整个进程中只会由某一个线程执行一次，之后其它线程不会再执行。 */
    assert(pthread_once(&key_once, _uthread_key_create) == 0);

    sched = _sched_get();
    if (sched == NULL) {
        /* 第一次调用uthread_create要初始化整个系统 */
        _runtime_init();
        if ((sched = _sched_get()) == NULL) {
            perror("Failed to create scheduler");
            return -1;
        }
        // 第一次调用uthread_create还需要为main函数创建一个uthread
        _uthread_create_main();
    }

    /* 执行用户发起的创建请求 */
    if ((ut = calloc(1, sizeof(struct uthread))) == NULL) {
        perror("Failed to allocate memory for new uthread");
        return errno;
    }
    assert(pthread_mutex_lock(&sched->global->mutex) == 0);
    sched->global->n_uthread++;         // 修改全局数据要加锁
    assert(pthread_mutex_unlock(&sched->global->mutex) == 0);

    if (posix_memalign(&ut->stack, getpagesize(), STACK_SIZE)) {    // 从堆上为协程分配栈空间
        free(ut);
        perror("Failed to allocate stack for new uthread");   
        return errno;
    }
    ut->stack_size = STACK_SIZE;
    ut->status = BIT(UT_ST_NEW);
    ut->func = func;
    ut->arg = arg;
    ut->is_main = 0;
    *new_ut = ut;
    
    TAILQ_INSERT_TAIL(&sched->p->ready, ut, ready_next);

    return 0;
}

// yield只做一次switch切换上下文（将协程放回队列的事儿改为交给resume去做，这样yield更纯粹）
void
_uthread_yield() {
    struct uthread *ut = _sched_get()->cur_uthread;
    _switch(&_sched_get()->ctx, &ut->ctx);
}

// 协程需要执行的全部流程：1）绑定的函数；2）设置EXITED状态位；3）yield回到调度器
static void
_uthread_exec(void *ut)
{
    ((struct uthread *)ut)->func(((struct uthread *)ut)->arg);
    
    /* 协程的函数体执行完后，需要更改协程的状态，然后yield */
    ((struct uthread *)ut)->status |= BIT(UT_ST_EXITED);
    _uthread_yield();   // 协程执行完后会通过yield回到调度器！
}

// 在resume中于_switch之前调用，初始化uthread的上下文，
static void
_uthread_init(struct uthread *ut)
{
    void **stack = (void **)(ut->stack + (ut->stack_size));    // stack是指针数组；另外，栈是从上到下的

    stack[-3] = NULL;
    stack[-2] = (void *)ut;
    ut->ctx.esp = (void *)stack - (4 * sizeof(void *));     // 栈的起始位置为stack下移4个void指针大小
    ut->ctx.ebp = (void *)stack - (3 * sizeof(void *));     // ebp存放函数调用的帧指针，【但这个初始值是否合理？】
    ut->ctx.eip = (void *)_uthread_exec;
    ut->status = BIT(UT_ST_READY);   // 注意这里是=而不是|=，直接把NEW状态也清除了
}

static void 
_uthread_free(struct uthread *ut) {
    free(ut->stack);
    free(ut);
    struct sched *sched = _sched_get();
    assert(pthread_mutex_lock(&sched->global->mutex) == 0);
    _sched_get()->global->n_uthread--;
    assert(pthread_mutex_unlock(&sched->global->mutex) == 0);
}

static void 
_uthread_free_main(struct uthread *ut) {
    free(ut);   // main协程不用堆空间作为自己的栈空间，无需释放栈空间
    struct sched *sched = _sched_get();
    assert(pthread_mutex_lock(&sched->global->mutex) == 0);
    _sched_get()->global->n_uthread--;
    assert(pthread_mutex_unlock(&sched->global->mutex) == 0);
}

int
_uthread_resume(struct uthread *ut) {
    struct sched *sched = _sched_get();

    if (ut->status & BIT(UT_ST_NEW)) 
        _uthread_init(ut);

    sched->cur_uthread = ut;
    _switch(&ut->ctx, &sched->ctx);

    sched->cur_uthread = NULL;

    if (ut->status & BIT(UT_ST_EXITED)) {
        ut->is_main ? _uthread_free_main(ut) : _uthread_free(ut);
    } else {
        TAILQ_INSERT_TAIL(&sched->p->ready, ut, ready_next); // 若不是EXITED状态，还需要把ut放回ready队列
    }

    return 0;
}

/* 用于main函数的末尾，防止main函数在其它协程之前退出，导致整个进程结束而其它协程还没有执行完毕。
 * 通过注册UT_ST_EXITED位，调度器会删除main协程  
 */
void 
uthread_main_end() {
    struct uthread *ut_main = _sched_get()->cur_uthread;
    ut_main->status |= BIT(UT_ST_EXITED);   // 这样调度器就会删掉main协程了
    _uthread_yield();
}

// 【先不区分读的时候会不会阻塞，只把p解绑，从全局中取一个M出来与p绑定】
ssize_t 
uthread_io_read(int fd, void *buf, size_t nbytes) {
    struct sched *cur_sched = NULL, *new_sched = NULL;
    struct global_data *global = NULL;
    pthread_t t;

    cur_sched = _sched_get();
    global = cur_sched->global;

    if (cur_sched->p && !TAILQ_EMPTY(&cur_sched->p->ready)) {
        new_sched = TAILQ_FIRST(&global->sched_idle);
        TAILQ_REMOVE(&global->sched_idle, new_sched, ready_next);
        global->n_sched_idle--;
        new_sched->p = cur_sched->p;
        cur_sched->p = NULL;

        /* 为新调度器创建一个线程 */
        printf("creating a new thread for blocked io...\n");
        assert(pthread_create(&t, NULL, _sched_create_another, new_sched) == 0);
        printf("created successively!\n");
    }
    
    // /* 以下单纯为了演示多线程并行的效果 */
    // for (int k = 0; k < 10000000; ++k) {
    //     printf("uthread a: k is %d\n", k);
    // }
    // /* 演示end */

    ssize_t res = read(fd, buf, nbytes);
    return res;
}

// 
ssize_t 
uthread_io_write(int fd, void *buf, size_t nbytes) {
    return write(fd, buf, nbytes);
}