#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../uthread.h"

int client()
{
    struct sockaddr_in serverAddr;
    int nFd = 0;
    int nRet = 0;
    int nReadLen = 0;
    char szBuff[BUFSIZ] = {0};

    /* 创建套接字描述符 */
    nFd = uthread_socket(AF_INET,SOCK_STREAM,0);
    if (-1 == nFd)
    {
        perror("socket:");
        return -1;
    }
    printf("create sucess sockfd: %d\n", nFd);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(12345);//默认以8080端口连接

    /* 和服务器端建立连接 */
    nRet = uthread_connect(nFd,(struct sockaddr*)&serverAddr,sizeof(serverAddr));
    if (nRet == -1)
    {
        printf("connect fail\n");
        return -1;
    }
    else{
        printf("client connect sucessful\n");
    }

    while(1)
    {
        /* 从终端读取数据 */
        memset(szBuff,0,BUFSIZ);
        nReadLen = read(STDIN_FILENO,szBuff,BUFSIZ);
        if (nReadLen > 0)
        {
            write(nFd,szBuff,strlen(szBuff));
        }
    }
    return 0;
}

void* myclient(void* data)
{
    client();
    return NULL;
}

int main()
{
    struct uthread* pid;
    uthread_create(&pid, myclient, NULL);
    uthread_main_end();

    return 0;
}