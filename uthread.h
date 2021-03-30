#ifndef UTHREAD_H
#define UTHREAD_H

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>

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
int uthread_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

ssize_t uthread_read(int fd, void *buf, size_t length);
ssize_t uthread_recv(int fd, void *buf, size_t length, int flags);
ssize_t uthread_recvmsg(int fd, struct msghdr *message, int flags);
ssize_t uthread_recvfrom(int fd, void *buf, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
ssize_t uthread_recv_exact(int fd, void *buf, size_t length, int flags) ;
ssize_t uthread_read_exact(int fd, void *buf, size_t length);

ssize_t uthread_write(int fd, const void *buf, size_t length);
ssize_t uthread_send(int fd, const void *buf, size_t length, int flags);
ssize_t uthread_sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t uthread_sendto(int fd, const void *buf, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t uthread_writev(int fd, struct iovec *iov, int iovcnt);


// 下面的接口临时开放给用户
void _uthread_yield();
void uthread_main_end();


#endif