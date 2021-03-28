#ifndef UTHREAD_INNER_H
#define UTHREAD_INNER_H

#include <inttypes.h>
#include <time.h>           // size_t
#include <pthread.h>
#include <sys/queue.h>      // for queue.h
#include <sys/time.h>   // gettimeofday
#include <sys/epoll.h>

#include "tree.h"
// #include "timer.h"

#define BIT(x) (1 << (x))
#define CLEARBIT(x) ~(1 << (x))

#define STACK_SIZE          (128*1024)   // 每个协程设置的栈空间大小
                                        // 【这个栈要是小了，比如2*1024，好像会出问题，先设得大些避开这个问题，后续再优化】
#define MAX_COUNT_SCHED     1000         // 最大允许创建的调度器个数（即线程的个数）
#define MAX_PROCS           4            // 最大允许并发的任务个数 ，也即初始化系统时创建的p和sched的个数
#define MAX_COUNT_UTHREAD   10000000     // 最大允许创建的ut个数，一千万

enum uthread_event {
    UT_EVENT_RD,
    UT_EVENT_WR,
};

struct context {                        // 直接copy来的
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
    UT_ST_SLEEPING,                     // “阻塞”状态
    UT_ST_DETACHED,                     // 处于分离状态的协程可以完全销毁，释放所有资源
    UT_ST_EXPIRED,                      // 被“阻塞”的协程因为超时而醒来
    UT_ST_FDEOF,                       // 被“文件结束”阻塞后醒来的
    UT_ST_WAIT_RD,
    UT_ST_WAIT_WR,
};

/* 【sched和p的状态后续再完善，暂时设置得比较随意】 */
enum sched_st {
    SCHED_ST_IDLE,
    SCHED_ST_RUNNING,
};
enum p_st {     
    P_ST_IDLE,
    P_ST_RUNNING,
    P_ST_SYSCALL,   // 暂未用到
    P_ST_DEAD,      // 暂未用到
};

/* 协程（或者称之为用户线程），相当于G */
// NOTE: 一定要让ctx作为第一个字段，因为_uthread_init中的上下文初始化并不适用于64位机器
// NOTE: 必须初始化的字段一定要在_uthread_crete_main和uthread_create中进行初始化
struct uthread {
    struct context          ctx;            // 协程的上下文
    unsigned long           id;             // 和线程一样，协程也必须有自己的id
    void                    *stack;         // 协程的栈，在堆上分配
    size_t                  stack_size;        
    uthread_func            func;
    void                    *arg;
    enum uthread_st         status;
    TAILQ_ENTRY(uthread)    ready_next;     // 用于指示p中的ready uthread队列的前后节点，参见内核数据结构TAILQ的用法
    int                     is_main;        // 指示是否为关联main函数的协程，main协程的栈不在堆上，free的时候需要另外处理
    uint64_t                wakeup_time_usec;    // “假阻塞”后协程醒来的时间
    struct uthread          *ut_joined;     // 连接到自己的协程
    struct p                *p;             // 通过ut->p->sched可以获取到某个ut的调度器（_sched_get只用于获取当前ut的调度器)
    void                    **retval;       // 用于uthread在被join时，传递返回值给join到它的协程
    int64_t                 fd_wait;        // 协程监听的 文件描述符+事件 组成的一个索引值，用于红黑树排序的唯一索引
    int                     is_wating_yield_signal;     // 是否允许被定时器唤醒
    
    RB_ENTRY(uthread)       sleep_node;     // rb树上的结点指针
    RB_ENTRY(uthread)       wait_node;      
};

// 声明结构体
TAILQ_HEAD(uthread_que, uthread);   // 带尾指针的uthread队列。结构体的名字为uthread_que，之后可通过struct uthread_que来定义一个队列  
RB_HEAD(uthread_rb_sleep, uthread); // sleeping tree
RB_HEAD(uthread_rb_wait, uthread); // waiting tree

/* 相当于P */
struct p {
    int32_t                 id;             // （测试用）
    pthread_t               tid;            //与P绑定的线程id
    enum p_st               status;         // 状态可为pidle，prunning，psyscall，后续再完善
    struct uthread_que      ready;          // p中的可运行uthread队列
    struct uthread_rb_sleep sleeping;       // p中被“阻塞”的协程
    struct uthread_rb_wait  waiting;
    TAILQ_ENTRY(p)          ready_next;     // 【取名为idle_next比较好，来不及改了】用于指示系统中idle p队列的前后节点，参见内核数据结构TAILQ的用法
    struct sched            *sched;         // 通过ut->p->sched可以获取到某个ut的调度器（_sched_get只用于获取当前ut的调度器)
    int                     poller_fd;
    struct epoll_event      eventlist[1024];
    int                     num_new_events;
};

TAILQ_HEAD(sched_que, sched); // 声明结构体，sched队列，将被定义在global_data中
TAILQ_HEAD(p_que, p);         // 声明结构体，p队列，将被定义在global_data中

/* 用于保存众多的全局变量，它会被注册在每个sched中，从而实现所有sched都可访问，
*  但在修改时要通过互斥量保证不冲突！！*/
struct global_data {
    pthread_mutex_t         mutex;           // 全局数据的锁

    /* 全局的sched数据 */
    struct sched            *all_sched;             // sched结构体数组
    uint32_t                count_sched;            // 系统中现有的sched的总数
    uint32_t                max_count_sched;        // 最大允许创建的sched个数
    struct sched_que        sched_idle;             // idle状态的sched队列
    struct sched_que        sched_with_stack;       // 分配了栈的sched
    char                    bitmap_sched[MAX_COUNT_SCHED];  // 位图，用于分配sched的id，偷懒的实现方式
    uint32_t                next_sched_id;          // 下一个可用的sched id

    /* 全局的p数据 */
    struct p                *all_p;                 // p结构体数组
    uint32_t                count_p;                // 创建的p的总数
    struct p_que            p_idle;                 // idle状态的p

    /* 全局的uthread数据 */
    uint32_t                n_uthread;              // 系统中的uthread总数
    long                    max_count_uthread;       // 最大允许创建的ut个数
    char                    bitmap_ut[MAX_COUNT_UTHREAD];  // 位图，偷懒。。
    unsigned long           next_ut_id;             // 下一个可用的ut id                        

    /* 后续可能需要创建全局的ready uthread队列 */
    // ...
};

/* 相当于M，主要承担（局部）调度器的功能，一个线程对应一个调度器 */
struct sched {
    int32_t                 id;                     // （测试用）
    enum sched_st           status;
    struct context          ctx;
    void                    *stack;
    size_t                  stack_size;
    uthread_func            func;                   // 局部sched的func只绑定sched_run
    void                    *arg;                   // （好像用不着。。）
    TAILQ_ENTRY(sched)      ready_next;             // 【取名idle next比较好，就不改了】用于指示系统中idle sched队列的前后节点，参见内核数据结构TAILQ的用法
    struct p                *p;                     // sched关联的p
    struct uthread          *cur_uthread;           // sched上正在运行的协程
    TAILQ_ENTRY(sched)      with_stack_next;        // 用于记录所有分配了栈的sched，以便最后统一释放sched的栈资源
};

int
_uthread_sleep_cmp(struct uthread *u1, struct uthread *u2);

int
_uthread_wait_cmp(struct uthread *ut1, struct uthread *ut2);

/* 在uthread.c中定义的全局变量，每个线程会拥有一份，用于绑定线程自己的sched（参见 线程特有数据 相关）*/
extern pthread_key_t uthread_sched_key;   

/* 声明运行时的全局数据，让包含头文件的地方都能访问它们 */
extern struct global_data *ptr_global;
extern struct sched all_sched[MAX_COUNT_SCHED];
extern struct p all_p[MAX_PROCS];

/* 框架内部使用的接口 */
struct sched* _sched_get();
int _runtime_init();
void _sched_run();
void * _sched_create_another(void *arg); 

void _uthread_yield();
int _uthread_resume(struct uthread *ut);

void _uthread_sched_sleep(struct uthread *ut, uint64_t mescs);

void _uthread_desched_sleep(struct uthread *ut);

void _uthread_sched_sleep(struct uthread *ut, uint64_t mescs); 

void _uthread_desched_sleep(struct uthread *ut);

void _register_event(struct uthread *ut, int sockfd, enum uthread_event e, uint64_t timeout);

/********************/

// 返回1970年1月1日到现在经过的时间（微秒）
static inline uint64_t
_get_usec_now() {
    struct timeval time = {0, 0};
    gettimeofday(&time, NULL);   // 1970年1月1日到现在经过的时间，精确到微秒
    return (time.tv_sec * 1000000) + time.tv_usec;  // 返回微秒，1秒=10^6微秒
}

#endif