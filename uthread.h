#ifndef UTHREAD_H
#define UTHREAD_H


int uthread_create(struct uthread **new_ut, void *func, void *arg);
void _uthread_main_end();

#endif