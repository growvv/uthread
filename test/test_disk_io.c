#include <stdio.h>
#include <unistd.h>
#include "uthread.h"

void *
a(void *arg)
{
    char c;
    // 输入ctrl + d退出循环
    while (pthread_disk_read(STDIN_FILENO, &c, 1)) {
        pthread_disk_write(STDOUT_FILENO, &c, 1);
    }
}

void *
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
    enable_hook();

    pthread_t p, p2;
    pthread_create(&p, NULL, a, NULL);
    pthread_create(&p2, NULL, b, NULL);

    main_end();
}
