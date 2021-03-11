#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>     // memset
#include <sys/epoll.h>

#include "uthread_inner.h"

#define FD_KEY(fd,e) (((int64_t)(fd) << (sizeof(int32_t) * 8)) | e)

struct sched* 
_sched_get() {
    return pthread_getspecific(uthread_sched_key);
}

static int 
_sched_work_done(struct p *p) {
    /* for debugging */
    // struct uthread *np = NULL;
    // int i = 0;
    // for (np = p->ready.tqh_first; np != NULL; np = np->ready_next.tqe_next) 
    //     i++;
    // printf("[sched id: %d] there are %d ready uthreads on current p [id: %d]\n", _sched_get()->id, i, p->id);
    /* end */

    return TAILQ_EMPTY(&p->ready);
}

/* 用于在整个进程工作结束后释放全局资源，一定要在调用函数之前上锁，防止多个线程同时释放 */
void free_source() {

    struct sched *sched = _sched_get(), *item;

    TAILQ_FOREACH(item, &ptr_global->sched_with_stack, with_stack_next) {
        free(item->stack);
    }
}

/* 调度器函数 */
void
_sched_run() {
    struct sched *sched = NULL;
    struct uthread *last_ready = NULL, *ut = NULL;

    sched = _sched_get();
start:
    while (sched->p && !_sched_work_done(sched->p)) {
        /* 执行就绪队列中的uthread */
        last_ready = TAILQ_LAST(&sched->p->ready, uthread_que);
        while (!TAILQ_EMPTY(&sched->p->ready)) {
            ut = TAILQ_FIRST(&sched->p->ready);
            TAILQ_REMOVE(&sched->p->ready, ut, ready_next);
            _uthread_resume(ut);
            if (!sched->p)          // NOTE：如果原来的p已经解绑了，那last_ready也就失去意义了
                break;
            if (ut == last_ready)         
                break;
        }
    }

    /* 若整个进程的所有uthread都运行完毕，则释放全局数据、退出进程 */
    if (ptr_global->n_uthread == 0) {       
        // 为输出最后的信息加锁
        assert(pthread_mutex_lock(&ptr_global->mutex) == 0); 
        free_source();
        printf("Congratulations, all uthreads done!\n");
        printf("Process is existing...\n");
        exit(0);        // 代替main函数的return语句结束整个进程
    } else 
        goto start;     // 如果系统中还有uthread在运行，让调度器空转（暂时用这种方式代替调度器休眠，后续再优化）    
}

/* 初始化整个运行时系统 */
/* WARNING：函数暂未对free失败的情况做除打印错误信息之外的任何有效处理，这有可能导致内存泄漏的！！ */
int
_runtime_init() {
    /* 初始化全局数据，注意顺序要和定义中保持一致，防止遗漏 */
    pthread_mutex_init(&ptr_global->mutex, NULL);

    ptr_global->all_sched = all_sched;
    ptr_global->count_sched = MAX_PROCS;    // 创建的sched的个数
    ptr_global->max_count_sched = MAX_COUNT_SCHED;
    TAILQ_INIT(&ptr_global->sched_idle);
    TAILQ_INIT(&ptr_global->sched_with_stack);
    memset(ptr_global->bitmap_sched, 0, sizeof(ptr_global->bitmap_sched));
    ptr_global->next_sched_id = MAX_PROCS;  // 下面会分配MAX_PROCS个sched，故此处直接设置该值
    
    ptr_global->all_p = all_p;
    ptr_global->count_p = MAX_PROCS;        // 创建的p的个数，后续不会改变
    TAILQ_INIT(&ptr_global->p_idle);
    ptr_global->max_count_uthread = MAX_COUNT_UTHREAD;
    memset(ptr_global->bitmap_ut, 0, sizeof(ptr_global->bitmap_ut));
    ptr_global->next_ut_id = 0;
    /* end -- 初始化全局数据 */

    /* 初始化sched和p数组，将它们分别插入全局的idle队列 */
    for (int i = 0; i < MAX_PROCS; ++i) {
        /* 初始化sched，入idle sched队列。调度器实际上全局有MAX_COUNT_SCHED个，但只初始化MAX_PROCS个 */
        struct sched *sched = &ptr_global->all_sched[i];
        if ((sched->stack = calloc(1, STACK_SIZE)) == NULL) {   // 为sched分配栈空间
            perror("Failed to allocate stack for sched");
            return errno;
        }
        sched->id = i;
        ptr_global->bitmap_sched[i] = 1;
        sched->status = BIT(SCHED_ST_IDLE);
        sched->stack_size = STACK_SIZE;
        TAILQ_INSERT_TAIL(&ptr_global->sched_idle, sched, ready_next);
        TAILQ_INSERT_TAIL(&ptr_global->sched_with_stack, sched, with_stack_next);
        
        /* 初始化p，入idle p队列 */
        struct p *new_p = &ptr_global->all_p[i];
        new_p->id = i;
        new_p->status = BIT(P_ST_IDLE);
        TAILQ_INIT(&new_p->ready);
        RB_INIT(&new_p->sleeping);
        RB_INIT(&new_p->waiting);
        TAILQ_INSERT_TAIL(&ptr_global->p_idle, new_p, ready_next);
    }    

    /* 拿出一个sched，现在就要用 */
    struct sched *first_sched = TAILQ_FIRST(&ptr_global->sched_idle);
    TAILQ_REMOVE(&ptr_global->sched_idle, first_sched, ready_next);
    first_sched->status = BIT(SCHED_ST_RUNNING);
    /* 为sched分配一个p */
    first_sched->p = TAILQ_FIRST(&ptr_global->p_idle);                      
    TAILQ_REMOVE(&ptr_global->p_idle, first_sched->p, ready_next);
    first_sched->p->status = BIT(P_ST_RUNNING);
    first_sched->p->sched = first_sched;    // 设置p所属的调度器
    first_sched->p->poller_fd = epoll_create(1024); // linux会忽略掉这个参数

    /* 此后，线程就可以通过_sched_get()获取自己的调度器了 */
    assert(pthread_setspecific(uthread_sched_key, first_sched) == 0);   

    /* 为调度器初始化上下文，直接搬的_uthread_init中的代码 */
    void **stack = (void **)(first_sched->stack + (first_sched->stack_size));   
    stack[-3] = NULL;
    stack[-2] = (void *)first_sched;
    first_sched->ctx.esp = (void *)stack - (4 * sizeof(void *));     
    first_sched->ctx.ebp = (void *)stack - (3 * sizeof(void *)); 
    first_sched->ctx.eip = (void *)_sched_run;

    return 0;
}

/* 用作创建一个新线程时要绑定的函数，该函数是对_sched_run的封装，目的是为了初始化新线程上的uthread_sched_key */
void *
_sched_create_another(void *new_sched) {
    struct sched *sched = (struct sched *)new_sched;
    assert(pthread_setspecific(uthread_sched_key, sched) == 0);
    _sched_run();
}


// ******************************************************
// schedler和uthread库都会用到的一些函数放在这里
void _register_event(struct uthread *ut, int sockfd, enum uthread_event e, uint64_t timeout) {
    struct epoll_event new_event;
    enum uthread_st status;

    new_event.data.fd = sockfd;
    if (e == UT_EVENT_RD) {
        status = UT_ST_WAIT_RD;
        new_event.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
    } else if (e == UT_EVENT_WR) {
        status = UT_ST_WAIT_WR;
        new_event.events = EPOLLOUT | EPOLLONESHOT | EPOLLRDHUP;
    }
    assert(epoll_ctl(_sched_get()->p->poller_fd, EPOLL_CTL_ADD, sockfd, &new_event) == 0);

    ut->status |= BIT(status);
    ut->fd_wait = FD_KEY(sockfd, e);    // fd_wait是为了让红黑树上可以有键值
    RB_INSERT(uthread_rb_wait, &ut->p->waiting, ut);
    // if (timeout == -1)  // 先假定timeout一定为正数
    //     return; 
    _uthread_sched_sleep(ut, timeout);
    ut->fd_wait = -1;
    ut->status &= CLEARBIT(status);
}
