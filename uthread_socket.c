#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>

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
                    printf("连接超时\n");
                    // return -1;
                }
                continue;
            }
            return -1;
        } else
            return res;
    }
}