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

#include "uthread.h"

int client()
{
    struct sockaddr_in serverAddr;
    int nFd = 0;
    int nRet = 0;
    int nReadLen = 0;
    char szBuff[BUFSIZ] = {0};

    /* 创建套接字描述符 */
    nFd = socket(AF_INET,SOCK_STREAM,0);
    if (-1 == nFd)
    {
        perror("socket:");
        return -1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(54500);     //默认以8080端口连接

    /* 和服务器端建立连接 */
    nRet = connect(nFd,(struct sockaddr*)&serverAddr,sizeof(serverAddr));
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

int server()
{
    struct sockaddr_in serveraAddr , clientAddr;
    socklen_t clientAddrLen;
    int nFd = 0,linkFd = 0;
    int nRet = 0;
    int nReadLen = 0;
    char szBuff[BUFSIZ] = {0};

    /* 创建一个socket描述符 */
    nFd = socket(AF_INET,SOCK_STREAM,0);
    if(-1 == nFd)
    {
        perror("socket create fail\n");
        return -1;
    }

    /* 给本地的socket地址赋值 */
    memset(&serveraAddr,0,sizeof(struct sockaddr_in));
    serveraAddr.sin_family = AF_INET; //以ipv4协议进行连接
    serveraAddr.sin_addr.s_addr = htonl(INADDR_ANY); //接收所有客户端ip的连接
    serveraAddr.sin_port = htons(54500);//默认接收8080端口发来的连接

    /* 当TCP的连接的状态是TCP_WAIT状态的时候， 可以通过设置SO_REUSEADDR
        选项来强制使用属于TIME_WAIT状态的连接的socket*/
    int isReuse = 1;
    setsockopt(nFd,SOL_SOCKET,SO_REUSEADDR,(const char*)&isReuse,sizeof(isReuse));

    /* 将该socket的描述符和本地的套接字地址绑定起来 */
    nRet = bind(nFd,(struct sockaddr*)&serveraAddr,sizeof(serveraAddr));
    if(-1 == nRet)
    {
        perror("bind fail");
        return -1;
    }

    /* 设置该套接口在监听状态 */
    listen(nFd,1);

    /* 等待客户端发来的tcp连接 ,当客户端连接进来之后，返回两个之间的唯一的socket连接，存放在linkFd之中*/
    clientAddrLen = sizeof(struct sockaddr_in);
    linkFd = accept(nFd,(struct sockaddr*)&clientAddr,&clientAddrLen);
    if(-1 == linkFd)
    {
        perror("accept fail");
        return -1;
    }

    /* 把连接进来的客户端地址和端口打印出来 */
    printf("commect %s %d successful\n",inet_ntoa(clientAddr.sin_addr),ntohs(clientAddr.sin_port));

    /* 循环的读取客户端发来的数据 */
    while(1)
    {
        memset(szBuff,0,BUFSIZ);
        nReadLen = read(linkFd,szBuff,BUFSIZ);
        if(nReadLen > 0)
        {
            printf("read data: %s\n",szBuff);
        }
    }

    return 0;
}

void* myserver(void* data)
{
    server();
    return NULL;
}
void* myclient(void* data)
{
    client();
    return NULL;
}

int main() {  
    enable_hook();

    pthread_t server_p, client_p;
    pthread_create(&server_p,NULL, myserver, NULL);
    sleep(1);
    pthread_create(&client_p,NULL, myclient, NULL);

    main_end();
}