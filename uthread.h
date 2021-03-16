#ifndef UTHREAD_H
#define UTHREAD_H

#include <sys/types.h>
#include <sys/socket.h>

struct uthread *ut;

int uthread_create(struct uthread **new_ut, void *func, void *arg);
ssize_t uthread_io_read(int fd, void *buf, size_t nbytes);
ssize_t uthread_io_write(int fd, void *buf, size_t nbytes);
int uthread_join(struct uthread *ut, void **retval);
unsigned long uthread_self(void);
void uthread_exit(void *retval);

// 开放给socket的接口
int uthread_socket(int domain, int type, int protocol);
int uthread_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// connect有可能超时
int uthread_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

// 下面的接口临时开放给用户
void _uthread_yield();
void uthread_main_end();


#endif