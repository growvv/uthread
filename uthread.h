#ifndef UTHREAD_H
#define UTHREAD_H

#include <sys/types.h>

struct uthread *ut;

int uthread_create(struct uthread **new_ut, void *func, void *arg);
ssize_t uthread_io_read(int fd, void *buf, size_t nbytes);
ssize_t uthread_io_write(int fd, void *buf, size_t nbytes);
int uthread_join(struct uthread *ut, void **retval);
unsigned long uthread_self(void);
void uthread_exit(void *retval);

// 下面的接口临时开放给用户
void _uthread_yield();
void uthread_main_end();


#endif