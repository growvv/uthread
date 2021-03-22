#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>  // read

#include "uthread_inner.h"

int 
uthread_socket(int domain, int type, int protocol) {
    int sock_fd;
    assert((sock_fd = socket(domain, type, protocol)) != -1);
    assert(fcntl(sock_fd, F_SETFL, O_NONBLOCK) != -1);
    return sock_fd;
}


// accept永不超时
int
uthread_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int res;
    struct uthread *ut = _sched_get()->cur_uthread;

    printf("uthraed_accept id: %d\n", (int)ut->id);

    while (1) {
        res = accept(sockfd, addr, addrlen);
        printf("uthraed_accept res: %d\n", res);
        if (res == -1) {
            // 若现在没有收到连接、若无法继续创建一个新的fd，“阻塞”协程，并注册一个读事件
            if (errno == EAGAIN || errno == ENFILE || errno == EMFILE) {
                _register_event(ut, sockfd, UT_EVENT_RD, 0);    // timeout为0，永远不会超时唤醒
                continue;
            } else if (errno == ECONNABORTED) {
                perror("A connection has been aborted");
                continue;
            } else {
                perror("Cannot accept connection");
                return -1;
            }
        }    
        return res; // 创建套接字成功
    }
}

// connect有可能超时
int uthread_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int res;
    struct uthread *ut = _sched_get()->cur_uthread;

    while (1) {
        res = connect(sockfd, addr, addrlen);
        printf("connect: %d\n", res);
        if (res == -1) {
            if (errno == EAGAIN || errno == EINPROGRESS) {
                printf("error: %d\n", errno);
                _register_event(ut, sockfd, UT_EVENT_WR, 10000); // 要设置超时时间 
                if (ut->status & BIT(UT_ST_EXPIRED)) {  // 如果connect超时
                    errno = ETIMEDOUT;
                    // printf("连接超时\n");
                    return -1;
                }
                continue;
            }
            return -1;
        } else
            return res;
    }
}


// 用于封装read族和recv族的接口，但是为什么使用while？                                
ssize_t uthread_read(int fd, void *buf, size_t length, uint64_t timeout) {
    ssize_t ret = 0;                                       
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {     
        printf("非阻塞读\n");
        if (ut->status & BIT(UT_ST_FDEOF))                   \
            return (-1);                                    \
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
        ret = read(fd, buf, length);                 
        printf("ret: %d\n", (int)ret);
        if (ret == -1 && errno != EAGAIN) {
            return (-1);                                    \
        }
        if ((ret == -1 && errno == EAGAIN)) {               \
            // printf("怎么才能触发您啊？？\n");
            _register_event(ut, fd, UT_EVENT_RD, timeout);  \
            if (ut->status & BIT(UT_ST_EXPIRED))             \
                return (-2);                                \
        }                                                   \
        if (ret >= 0)                                       \
            return (ret);                                   \
    }                                                       \
}  


// 用于封装write和send族接口
ssize_t uthread_write(int fd, const void *buf, size_t length)  {                                                        
    ssize_t ret = 0;
    ssize_t sent = 0;
    struct uthread *ut = _sched_get()->cur_uthread;
    while (sent != length) {
        if (ut->status & BIT(UT_ST_FDEOF))
            return (-1);
        ret = write(fd, ((char *)buf) + sent, length - sent);                                         
        if (ret == 0)
            return (sent);
        if (ret > 0)
            sent += ret;
        if (ret == -1 && errno != EAGAIN)
            return (-1);
        if (ret == -1 && errno == EAGAIN)
            _register_event(ut, fd, UT_EVENT_WR, 0);
    } 
    return (sent);
}  