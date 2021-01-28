#ifndef UTHREAD_H
#define UTHREAD_H

struct uthread *ut = NULL;

int uthread_create(struct uthread **new_ut, void *func, void *arg);

// 下面的接口临时开放给用户
void _uthread_yield();
void uthread_main_end();

#endif