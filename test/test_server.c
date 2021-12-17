#include<stdio.h>
#include<errno.h>
#include<err.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include "uthread.h"

/* Port to listen on. */
#define SERVER_PORT 5555
/* Connection backlog (# of backlogged connections to accept). */
#define CONNECTION_BACKLOG 8

void* doServerStuff(void*);
void* hello(void*);

int main() {

    enable_hook();

    int listenfd, clientfd;
    struct sockaddr_in listen_addr;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err(1, "listen failed");
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(SERVER_PORT);
    if (bind(listenfd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        err(1, "bind failed");
    }

    if (listen(listenfd, CONNECTION_BACKLOG) < 0) {
        err(1, "listen failed");
    }

    pthread_t init_p;
    pthread_create(&init_p, NULL, hello, NULL); // 为了在调用accept前创建runtime

    while(1) {
        if ( (clientfd = accept(listenfd, (struct sockaddr*)NULL, NULL)) == -1) {
                printf("accept socket error:%s(errno:%d)\n", strerror(errno), errno);
                continue;
        }
        printf("accept a new client:%d\n", clientfd);
        pthread_t p;
        pthread_create(&p,NULL, doServerStuff, &clientfd);
    }

    main_end();
}

void* doServerStuff(void *arg) {
    int clientfd = *(int *)arg;
    char buf[1024];
    int n;
    while(1) {
        printf("clientfd:%d\n", clientfd);
        n = read(clientfd, buf, sizeof(buf));
        if (n == 0) {
            printf("client close\n");
            break;
        }
        write(clientfd, buf, n);
    }
}

void* hello(void* arg) {
    printf("init\n");
    return NULL;
}