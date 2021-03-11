#include "uthread.h"
#include <stdio.h>
#include <unistd.h>

void
a(void *arg)
{
    char c;
    // 输入ctrl + d退出循环
    while (uthread_io_read(STDIN_FILENO, &c, 1)) {
        uthread_io_write(STDOUT_FILENO, &c, 1);
    }
}

void
b(void *x)
{
    for (int i = 0; i < 10; ++i) {
        sleep(1);   // 方便效果演示
        printf("uthread b: i is %d\n", i);
    }
}

int
main(int argc, char **argv)
{
    struct uthread *ut = NULL;

    uthread_create(&ut, a, NULL);
    uthread_create(&ut, b, NULL);
    _uthread_yield();

    uthread_main_end();
}
