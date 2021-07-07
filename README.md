# 7-湖上摸鱼家

湖北上海，两地摸鱼 

#### 介绍

这是一个纯C实现的POSIX风格的协程库，具有轻量、高效、用户无感知的特点。

主要特性：
1. 128k协程栈的轻量级协程，通过汇编实现用户态的上下文切换
2. 基于GMP架构，多个协程复用多个线程，兼具灵活、高效的协程管理方式
3. 通过对pthread接口以及socket I/O库函数进行hook，保证用户无感知
4. 基于epoll与非阻塞式I/O实现协程级别的socket读写
5. 基于红黑树实现对定时休眠任务的管理
6. 通过将任务集合转移到新的线程上，使得阻塞系统调用不会干扰其它协程的执行
7. 基于时间轮定时器实现10ms级的抢占

#### 软件架构

##### GMP架构
我们主要参考Golang的GMP模型进行项目代码的架构设计，架构示意图如下：

<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image.png" width:50%; height:50% />
</div>

架构涉及三个核心组件，图中KSE为内核调度实体，即内核线程。三个组件简要说明如下：
- ut：协程实体，是运行时系统调度的基本单位；因协程在某种意义上也可以被理解为“用户线程”，所以此处是取user thread之意，在编码时协程的结构体被命名为uthread。  
- p：对单个线程上所有协程任务的封装，包括就绪任务、阻塞任务、定时的休眠任务。  
- sched：协程的调度器，一个内核线程与一个调度器一一对应。  

##### 接口层次设计
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/20210331212521.png" width:50%; height:50% />
</div>

##### 调度逻辑和协程状态流转
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/20210331232701.png" width:50%; height:50% />
</div>

#### 使用说明

1. 编辑库配置文件/etc/ld.so.conf.d/usr-libs.conf，写入库文件所在目录/usr/local/lib  
2. 执行命令行ldconfig更新/etc/ld.so.cache文件
3. 在代码中包含uthread.h头文件

```C
#include <stdio.h>
#include "uthread.h"

// 测试协程基本的让出和恢复执行
void *
a (void *x) {
    for (int i = 0; i < 100; i = i + 2) {
        printf("thread a: %d\n", i);
    }
}

void * 
b (void *x) {
    for (int i = 1; i < 100; i = i + 2) {
        printf("thread b: %d\n", i);
    }
}

int
main (int argc, char **argv) {

    enable_hook();
    
    pthread_t p1,p2;
    pthread_create(&p1,NULL,a,NULL);
    pthread_create(&p2,NULL,b,NULL);

    printf("main is running...\n");
    printf("main is exiting...\n");

    main_end();
}

```

# 获奖情况


2020 openEuler 高校开发者大赛 前 50 强: https://mp.weixin.qq.com/s/MgVNeS0hlNCqHDiWdyrygA
