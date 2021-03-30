#include<pthread.h>
#include<stdio.h>
#include <unistd.h>
#include "myhook.h"
#include "uthread.h"


void *func() {
    printf("function call begin\n");
    // pthread_exit(pthread_self());
    printf("function call running\n");
    printf("function call end\n");
}


void *
a(void *arg)
{
    char c;
    // 输入ctrl + d退出循环
    while (read(STDIN_FILENO, &c, 1)) {
        write(STDOUT_FILENO, &c, 1);
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


int main() {
    printf("in main\n");
    enable_hook();
    pthread_t p;//unsigned long int
    // create
    pthread_create(&p,NULL,func,NULL);
    
    printf("in join\n");
    // join
    pthread_join(p,NULL);
    printf("after join\n");
    printf("p = %ld\n",p);

    // end
    uthread_main_end();
    return 0;
}

// int main() {
//     pthread_t p;

//     pthread_create(&p,NULL, a, NULL);
//     pthread_create(&p,NULL, b, NULL);
//     _uthread_yield();

//     return 0;
// }