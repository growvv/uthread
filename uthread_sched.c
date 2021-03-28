#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>     // memset
#include <sys/epoll.h>
#include "tree.h"

#include "uthread_inner.h"
#include "timer.h"

#define FD_KEY(fd,e) (((int64_t)(fd) << (sizeof(int32_t) * 8)) | e)
#define FD_EVENT(f) ((int32_t)(f))
#define FD_ONLY(f) ((f) >> ((sizeof(int32_t) * 8)))

// 生成uthread_rb_sleep的红黑树操作
RB_GENERATE(uthread_rb_sleep, uthread, sleep_node, _uthread_sleep_cmp); // 生成 sleep uthread 的红黑树操作函数
RB_GENERATE(uthread_rb_wait, uthread, wait_node, _uthread_wait_cmp);

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

    return TAILQ_EMPTY(&p->ready) && RB_EMPTY(&p->sleeping) && RB_EMPTY(&p->waiting);
}

/* 用于在整个进程工作结束后释放全局资源，一定要在调用函数之前上锁，防止多个线程同时释放 */
void free_source() {

    struct sched *sched = _sched_get(), *item;

    TAILQ_FOREACH(item, &ptr_global->sched_with_stack, with_stack_next) {
        free(item->stack);
    }
}

// 将监听fd的那个lt从wait tree或者sleeping tree上移除
struct uthread *
_uthread_desched_event(int fd, enum uthread_event e)   
{
    struct uthread *ut = NULL;
    struct sched *sched = _sched_get();
    struct uthread find_ut;
    find_ut.fd_wait = FD_KEY(fd, e);

    ut = RB_FIND(uthread_rb_wait, &sched->p->waiting, &find_ut);
    if (ut != NULL) {
        RB_REMOVE(uthread_rb_wait, &sched->p->waiting, ut);    // 从waiting tree上移除
        printf("### 就绪 从waiting tree拿下：%d\n", (int)ut->id);
        _uthread_desched_sleep(ut);                             // 也将lt从sleeping tree上移除，以防lt在sleeping tree中
    }

    return (ut);
}


static void
_uthread_cancel_event(struct uthread *ut) {
    int fd = FD_ONLY(ut->fd_wait);
    struct epoll_event event;
    event.data.fd = fd;

    if (ut->status & BIT(UT_ST_WAIT_RD)) {
        event.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
        ut->status &= CLEARBIT(UT_ST_WAIT_RD);
    } else if (ut->status & BIT(UT_EVENT_WR)) {
        event.events = EPOLLOUT | EPOLLONESHOT | EPOLLRDHUP;
        ut->status &= CLEARBIT(UT_EVENT_WR);
    }
    // 先取消对fd的监听
    assert(epoll_ctl(ut->p->poller_fd, EPOLL_CTL_DEL, fd, &event) == 0);
    // printf("--- sleep 取消监听\n");
    // 再把fd从waiting树上拿下
    if (ut->fd_wait >= 0) {
        _uthread_desched_event(FD_ONLY(ut->fd_wait), FD_EVENT(ut->fd_wait));
        printf("### 从waiting tree拿下：%d\n", (int)ut->id);
    }
    ut->fd_wait = -1;
}

static void
_uthread_resume_expired(struct sched *sched)
{
    struct uthread *ut = NULL;
    //struct lthread *lt_tmp = NULL;
    uint64_t t_diff_usecs = 0;

    /* current scheduler time */
    // t_diff_usecs = _lthread_diff_usecs(sched->birth, _lthread_usec_now());  // 【lfr】因为lt->sleep_usecs赋值的时候也减掉了birth
        printf("here!\n");

    while (sched->p && (ut = RB_MIN(uthread_rb_sleep, &sched->p->sleeping)) != NULL) {
        printf("您怎么一直在这sleep啊\n"); // 错怪您了
        if (ut->wakeup_time_usec <= _get_usec_now()) {  //  【lfr】sleep完了
            _uthread_cancel_event(ut);
            _uthread_desched_sleep(ut);    // 从sleep tree上移除
            ut->status |= BIT(UT_ST_EXPIRED);

            printf("resume 因expired\n");
            _uthread_resume(ut);

            continue;
        }
        break;
    }
}

// 
static uint64_t
_uthread_min_timeout(struct sched *sched)
{
    uint64_t t_diff_usecs = 0, min = 0;

    min =  1000000u;                     //sched->default_timeout; // sleep树上没有东西，等3秒

    struct uthread *ut = RB_MIN(uthread_rb_sleep, &sched->p->sleeping);
    if (!ut)
        return (min);                           // 如果没有被阻塞的lthread，就返回默认超时时间

    min = ut->wakeup_time_usec;
    if (min > _get_usec_now())
        return (min - _get_usec_now());

    return (0);
}


static int
_uthread_poll(void)
{
    struct sched *sched;
    sched = _sched_get();    // 获取当前lthread所属的调度器
    struct timespec t = {0, 0};     // 给下面的_lthread_poller_poll使用，作为epoll_wait的阻塞时间
    int ret = 0;
    uint64_t usecs = 0;

    sched->p->num_new_events = 0;
    usecs = _uthread_min_timeout(sched);  // 偏差
    printf("min_timeout: %lld\n", (long long)usecs);

    /* never sleep if we have an lthread pending in the new queue */
    // 如果_lthread_min_timeout返回0，或者就绪队列不为空，就直接返回，不会继续去获取POLL_EVENT_TYPE事件
    if (usecs && TAILQ_EMPTY(&sched->p->ready)) {
        // 【感觉这一段应该就是把微秒转换成秒+纳秒，但好像逻辑又不完全对】
        t.tv_sec =  usecs / 1000000u;   
        if (t.tv_sec != 0)              
            t.tv_nsec  =  (usecs % 1000000u)  * 1000u;  // 【就是这里，貌似写错了？——经讨论，是作者把两个数字写反了】已改
        else
            t.tv_nsec = usecs * 1000u;
    } else {
        return 0;               
    }

    // 不断尝试获取就绪的POLL_EVENT_TYPE事件，直到获取成功
    while (1) {
        printf("开始epoll_wait\n");
        ret = epoll_wait(sched->p->poller_fd, sched->p->eventlist, 1024, t.tv_sec*1000.0 + t.tv_nsec/1000000.0);    
        if (ret == -1 && errno == EINTR) {  // The call was interrupted by a signal handler before... 见官网，这是一个可接受的error 
            continue;
        } else if (ret == -1) {             // 其它不可接受的error
            perror("error adding events to epoll/kqueue");
            assert(0);
        }
        break;
    }

    // sched->nevents = 0;         // 【？】
    sched->p->num_new_events = ret;

    return (0);
}

void handle_event(int fd, enum uthread_event ev, int is_eof) {
    printf("收到读就绪\n");
    struct uthread* ut = _uthread_desched_event(fd, ev);  /* 将lt从sleeping tree或者waiting tree中移除 */ 
    
    if(ut == NULL) {
         printf("ut为NULL, %d\n", ev);
    } else {
        printf("ut:%d e:%d\n", (int)ut->id, ev);
    }
    if (ut == NULL)  return;    
    
    
    // int fd = FD_ONLY(ut->fd_wait);
    assert(fd == FD_ONLY(ut->fd_wait));
    struct epoll_event event;
    event.data.fd = fd;

    if (ut->status & BIT(UT_ST_WAIT_RD)) {
        event.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
        ut->status &= CLEARBIT(UT_ST_WAIT_RD);
    } else if (ut->status & BIT(UT_EVENT_WR)) {
        event.events = EPOLLOUT | EPOLLONESHOT | EPOLLRDHUP;
        ut->status &= CLEARBIT(UT_EVENT_WR);
    }
    // 先取消对fd的监听
    assert(epoll_ctl(ut->p->poller_fd, EPOLL_CTL_DEL, fd, &event) == 0);       
    // printf("--- wait 取消监听\n");                                                                   

    if (is_eof)                                                    
        ut->status |= BIT(UT_ST_FDEOF);                          
    _uthread_resume(ut);                                        

}


/* 调度器函数 */
void
_sched_run() {
    struct sched *sched = NULL;
    struct uthread *last_ready = NULL, *ut = NULL;

    sched = _sched_get();
start:
    // printf("always start\n");
    while (sched->p && !_sched_work_done(sched->p)) {
        /*处理seelp就绪的*/
        _uthread_resume_expired(sched);

        printf("还有任务呢\n");
        /* 执行就绪队列中的uthread */
        last_ready = TAILQ_LAST(&sched->p->ready, uthread_que);
        while (!TAILQ_EMPTY(&sched->p->ready)) {
            ut = TAILQ_FIRST(&sched->p->ready);
            printf("从ready取出ut: %d\n", (int)ut->id);
            TAILQ_REMOVE(&sched->p->ready, ut, ready_next);
            _uthread_resume(ut);
            if (!sched->p)          // NOTE：如果原来的p已经解绑了，那last_ready也就失去意义了
                break;
            if (ut == last_ready)         
                break;
        }

        if (!sched->p)
            break;
            
        printf("poll前\n");
        /*处理wait就绪的*/
        _uthread_poll(); 
        printf("poll后\n");

        while (sched->p && sched->p->num_new_events) {
            int id = --sched->p->num_new_events;
            int fd = sched->p->eventlist[id].data.fd;
            // fd为调度器本身
            // if (fd == sched->eventfd) {   
            //     _uthread_poller_ev_clear_trigger();
            //     continue;
            // }

            // 事件：文件描述符挂断
            int is_eof = ((sched->p->eventlist[id].events) & EPOLLHUP);  // 若事件为：对应的文件描述符被挂断了
            
            if(is_eof)
               errno = ECONNRESET;

            // 事件：读/写就绪
            printf("监听到的东西: %d %d %d %d\n", sched->p->eventlist[id].events & EPOLLIN, sched->p->eventlist[id].events & EPOLLOUT, sched->p->eventlist[id].events & EPOLLONESHOT, sched->p->eventlist[id].events & EPOLLHUP);
            handle_event(fd, UT_EVENT_RD, is_eof);
            handle_event(fd, UT_EVENT_WR, is_eof);
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
        new_p->tid = pthread_self();
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

    printf("create timewheel before\n");
    pthread_t tid;
    pthread_create(&tid,NULL,create_timewheel,NULL);
    printf("tid:%ld\n",(long int)tid);
    return 0;
}

/* 用作创建一个新线程时要绑定的函数，该函数是对_sched_run的封装，目的是为了初始化新线程上的uthread_sched_key */
void *
_sched_create_another(void *new_sched) {
    struct sched *sched = (struct sched *)new_sched;
    sched->p->tid = pthread_self();
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
    // assert(epoll_ctl(_sched_get()->p->poller_fd, EPOLL_CTL_ADD, sockfd, &new_event) == 0);
    epoll_ctl(_sched_get()->p->poller_fd, EPOLL_CTL_ADD, sockfd, &new_event);
    printf("ctl after\n");

    ut->status |= BIT(status);
    ut->fd_wait = FD_KEY(sockfd, e);    // fd_wait是为了让红黑树上可以有键值
    // printf("insert before\n");
    printf("添加到 waiiting tree: %d\n", (int)ut->id);
    RB_INSERT(uthread_rb_wait, &ut->p->waiting, ut);
    // printf("insert after\n");
    // if (timeout == -1)  // 先假定timeout一定为正数
    //     return; 
    printf("sleep before\n");
    _uthread_sched_sleep(ut, timeout);
    printf("sleep after\n");
    ut->fd_wait = -1;
    ut->status &= CLEARBIT(status);
}

inline int
_uthread_poller_ev_get_fd(struct epoll_event *ev)
{
    return (ev->data.fd);
}

inline int
_uthread_poller_ev_get_event(struct epoll_event *ev)
{
    return (ev->events);
}

inline int
_uthread_poller_ev_is_eof(struct epoll_event *ev)
{
    return (ev->events & EPOLLHUP);
}

// “阻塞”协程
void
_uthread_sched_sleep(struct uthread *ut, uint64_t mescs) {
    if (mescs == 0)
        mescs = (uint64_t)(1<<31-1) * 1000;

    ut->wakeup_time_usec = _get_usec_now() + mescs * 1000;
    assert(RB_INSERT(uthread_rb_sleep, &ut->p->sleeping, ut) == 0);
    printf("添加到 sleep tree: %d\n", (int)ut->id);
    ut->status |= BIT(UT_ST_SLEEPING);

    ut->is_wating_yield_signal = 0;
    printf("yield before\n");
    // 让出CPU，进入“阻塞”状态
    _uthread_yield();

    printf("wake up\n");
    // 协程醒过来之后
    ut->status &= CLEARBIT(UT_ST_SLEEPING);
}

// 将ut从所属p的sleeping树上摘下，修改ut的状态为ready
void
_uthread_desched_sleep(struct uthread *ut) {
    if (ut->status & BIT(UT_ST_SLEEPING)) {
        RB_REMOVE(uthread_rb_sleep, &ut->p->sleeping, ut);
        printf("### 从sleep tree拿下：%d\n", (int)ut->id);
        ut->status &= CLEARBIT(UT_ST_SLEEPING);
        ut->status |= BIT(UT_ST_READY);
    } 
}