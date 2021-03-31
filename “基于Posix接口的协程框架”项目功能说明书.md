# “基于Posix接口的协程框架”项目功能说明书

## 一·项目背景与成果简介

随着当前新的高并发场景下对服务器性能要求的不断提高，以往的多线程并发技术暴露出了不容忽视的局限性。协程以及用户态线程，这些很早就已经被提出的概念和技术在新的场景和需求下得到了复苏。

### 1.1 背景
#### 1.1.1 传统的并发模型

最早的时候，服务器还不能支持多线程，服务器用一个线程响应所有的客户端，且只能做到同步I/O，服务器把大量的时间花在等待I/O操作上。引入多线程并发后，服务器为每个客户端的连接都创建一个线程，当某个线程发起I/O操作时，若I/O不能马上进行，会进入阻塞状态，CPU切换到其他线程去响应来自其它客户端的请求，这样的并发模型得以节约时间。但是系统不得不为每一个线程分配它们需要的资源，当并发的量级太大时，内存资源开始捉襟见肘。  

线程池技术是对普通的多线程并发模型的一种改进，它并不为每一个连接都创建线程，而是让线程池中固定的几个工作线程去服务任务队列中待处理的请求。线程池技术解决了高并发下内存资源不足的问题，但一定程度上也限制了并发任务的量级。而且，线程池的异步编程方式要求用户以异步方式思考，增加了开发的难度。

#### 1.1.2 协程

由于历史原因，协程当初并没有得到重视，因为当时的函数间层次调用方式已经足够解决大部分问题，协程的思想违背了这种“自顶向下”的主流理念。协程本质上就是一个特殊的函数，这个函数可以在某个地方挂起，并且可以重新在挂起处继续运行。协程的切换完全由用户程序控制，不会像线程切换那样消耗资源。一个进程可以包含多个线程，一个线程也可以包含多个协程。简单来说，一个线程内可以有多个这样的特殊函数在运行。但是有一点必须明确的是，一个线程上多个协程的运行是串行的。当一个协程运行时，其它协程必须被“挂起”。  

在服务器场景下，协程的重新登场使得开发者能够重新按照最初多线程编程的方式，为每一个连接创建一个协程，而不用再担心创建的线程过多导致系统资源被大量消耗。相对于线程池技术，协程采用同步的编程方式，但却能基于协程的轻量级达到更佳的并发性能！腾讯用于微信后台的libco协程框架就是一个将并发由线程转移到协程一级的经典案例。

#### 1.1.3 用户线程和两级线程模型

现代操作系统基本都采用内核线程模型。在该方案中，内核负责管理线程，程序对线程的创建、终止和同步都必须通过内核提供的系统调用来完成。如前所述，在新的高并发需求下，这种线程模型的缺陷已经让人们愈发难以忍受。而在两级线程模型中，除了一个进程可以创建多个内核线程，一个内核线程还可以创建多个用户线程。用户线程所占的资源更少，且用户线程的切换不经过内核，大大减少了很多额外的开销。  

我们可以看出，协程在本质上是一种用户线程。只是同一个内核线程创建的多个协程是严格串行的，而且多个协程只能对应到一个内核线程。然而，Golang的goroutine方案作为用户线程的另一个成功案例，打破了协程只能够串行执行的原则。由同一个内核线程创建的多个goroutine不仅可以实现并发（快速切换），还可以直接让多个goroutine复用多个内核线程，达到goroutine一级的多核并行。也因为如此，Golang官方并不把调度实体称为“协程”，而是使用自己命名的“goroutine”。但就本质而言，我们将其视为协程并无不可。如后文所见，我们的调度实体更接近于goroutine，但我们选择不拘泥于概念上的无端束缚，仍将其称为协程。

### 1.2 成果简介 【用lfr的】

此次赛题要求我们实现一套基于posix接口的协程框架。我们通过linux提供的hook技术，在尽量不改变posix系统编程接口的基础上，实现了一套支持协程级别的并发和并行的轻量级协程框架（或者说两级线程框架）。用户仍将使用pthread和socket库的相关接口，但是实际创建和操纵的将是我们设计的协程。我们在最大程度上不改变用户的编程习惯，使用户可以完全不感知协程的存在。  

在基于线程的网络编程中，当一个TCP连接两端的线程不能够马上进行I/O动作时，线程会被阻塞，但这并不会妨碍其它并发线程的执行。在协程框架上，TCP连接两端的实体是粒度更小的协程，当协程无法马上进行I/O时，协程也会被“阻塞”。此时，线程会自动切换到其它协程上继续执行。当I/O事件就绪时，运行时系统会收到通知，并将被“阻塞”的线程重新放入就绪队列。因此，我们的框架可以良好地支持多连接的网络服务。  

在Golang中，所有的goroutine受到一种并不十分严格的抢占机制的约束。当有实体运行超过一段时间之后，相关状态位会被运行时系统的监控goroutine修改。当实体再次进行函数调用时会自动检测到这个状态位的变化，从而执行主动的让出操作。但如果实体永不调用任何函数，它就会一直占用资源而无法被运行时系统抢占。在我们的协程框架中，这种不严格的抢占被改为了完全约束的抢占机制。运行时通过一个专门的监控线程监控所有调度器上协程的运行情况。如果有运行时间过长的协程，监控线程会发送一个信号给目标线程，迫使其执行让出操作。此外，和Golang不同，我们的调度模型中并不包含全局调度器和全局任务队列，而是纯粹的分布式调度。  

## 二.架构与实现
### 2.1 核心架构

我们主要参考Golang的GMP协程调度模型进行项目代码的架构设计，架构示意图如下：
<div align = "center" >
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image.png" width=60% height=70% style="zoom:10%" />
</div>


架构涉及三个核心组件，图中KSE为内核调度实体，即内核线程。三个组件简要说明如下：
- ut：协程实体，是运行时系统调度的基本单位；因协程在某种意义上也可以被理解为“用户线程”，所以此处是取user thread之意，在编码时协程的结构体被命名为uthread。  
- p：对单个线程上所有协程任务的封装，包括就绪任务、阻塞任务、定时的休眠任务。  
- sched：协程的调度器，一个内核线程与一个调度器一一对应。  

从架构的示意图中可以看出，KSE与sched的关联是稳定的，它们之间存在一一对应的关系，系统中有多少个（用于运行协程的）内核线程就会有多少个调度器，且二者的关联是稳定的，不会在系统运行时发生变动。相对地，sched与p、p与ut的关联都是可变的，但在某一确定的时刻，它们的关联依然是一对一的。下面将详细介绍这三个核心组件。  

#### 2.1.1  uthread

作为运行时系统调度的基本单位，每一个协程都会维护自己的上下文信息。此外，每一个协程都拥有自己的栈空间，这些栈空间从进程的堆上分配而来。在协程让出时，需要保存自己的当前上下文，同时向 CPU寄存器中写入调度器的上下文；同理，在调度器决定执行某一个协程时，它需要恢复该协程的上下文。特别地，协程的上下文信息中也包含自己栈空间的栈顶位置。  

每一个协程实体在它生命周期中的不同阶段会具有不同的状态，协程进行状态转移的主要流程如下图所示：  

<div align = "center" >
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(1).png" width=50% height=80% style="zoom:10%;" />
</div>


处于UT_ST_RUNNING状态的协程可能会因为执行时间超过限制而被运行时进行轮转让出，也有可能因为尝试执行某一个不能马上开始的socket I/O函数而被“阻塞”，转为UT_ST_WAITING状态（这里的XX表示因为阻塞事件的不同会有不同的细分状态）。调度器会为处于UT_ST_WAITING_XX状态的uthread监听相关的文件描述符和对应的事件。如果这样的监听是带有时间限制的，那么uthread还会同时被标记为UT_ST_SLEEPING状态，调度器会在调度循环中对所有定时任务进行管理。当uthread执行完毕后，如果它自身没有调用hook之后的pthread_detach，又没有任何其它uthread对其执行hook后的ptread_join，那么这个uthread的大部分资源会被释放，但协程的id以及运行时中的ut位图资源将仍然被占用，直到有任何uthread对它进行了join操作。因此，一个uthread同线程和进程一样，也有可能会成为“僵尸协程”。  

#### 2.1.2 p

如前所述，p是对一些协程任务的封装。p中维护了一个就绪状态的协程队列，以及两棵用于存放特定状态的协程的红黑树——其中一棵存放处于休眠状态的协程，另一棵则存放那些因为试图执行还未就绪的socket I/O操作而被阻塞的协程。一般来说p会稳定地维持与某个调度器的关联，但如后文所述，在某些情况下需要将p这个任务集合整体转移到其它的调度器上。和Golang相比，此处的p在“逻辑调度器”这一层面上的意义有所减弱，它更多地用于对任务进行集中管理。

#### 2.1.3 sched  

在运行时系统中，多个协程复用多个线程，每一个用于运行协程的工作线程都有自己的调度器。系统实际工作时，线程的执行流从调度器到某个协程，又从协程回到调度器，继而再次调度其它的协程。每一个调度器在工作时会绑定一个p，即这个调度器的任务集合，可以称p中的所有协程都属于该调度器。但在某些场景下，调度器可能不会绑定任何p。当某个正在运行的协程要执行一个阻塞的系统调用，比如进行对磁盘或者终端的读写时，会先将所属调度器上的p转移到其它空闲的调度器（线程）上，或者专门为之创建一个新的线程。此时，原调度器上除了要执行阻塞系统调用的协程不再有任何其它的协程。Golang中的M组件会绑定一个调度器，而我们的sched组件则直接代表调度器实体。  

值得一提的是，调度器也有自己的栈空间，当执行流位于调度循环上时，线程使用的将会是调度器的栈空间。因此，从某种意义上说，调度器和位于同一线程上的所有协程具有平等的地位。

### 2.2 技术要点
#### 2.2.1 运行时启动

用户在第一次执行hook后的pthread_create时，会自动调用_runtime_init进行整个运行时系统的初始化工作。  

首先，会初始化sched、p、ut三大核心组件相关的的全局数据结构，包括存储组件信息的全局数组、用于记录组件使用情况和后续分配的数据结构位图、创建调度器需要的栈空间；此外，还要为全局数据的访问初始化互斥锁。然后，为当前线程创建核心调度循环的上下文，并为当前线程绑定一个可用的sched与p。接着，创建时间轮监控线程，用于运行时系统的抢占机制——至此，运行时的大部分初始化工作就完成了。随后，系统会把main函数这个线程的执行流封装进一个uthread中，让main函数成为一个普通的协程。系统把main协程放入p的就绪队列，随即进行一次_switch调用切换到调度器。调度器开始执行并发现自己绑定的p中已经存在任务，马上从任务队列中取出main协程执行。此后，线程的执行流就遵循“协程-调度器-协程”的模式，整个系统的调度便以协程为粒度进行了。运行时系统启动的流程图如下：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(2).png" width: 100px; height : 100px style="zoom:50%;" />
</div>


#### 2.2.2 调度循环

每一个线程都会绑定一个调度器，绑定的实质是为线程绑定一个用于执行核心调度循环的_sched_run函数，以及为调度器的执行提供一个sched结构体。运行时初始化完毕之后，线程就始终运行在调度循环之中，执行流会在调度器和协程之间反复切换。从调度器的角度来看，调度循环主要分为四个环节：检查是否有到达唤醒时间的处于睡眠状态的任务、执行就绪队列中的任务、为阻塞在socket I/O上的协程监听相关的事件、处理监听到的就绪事件。核心调度循环的流程图如下：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(3).png" width=35% height=30% style="zoom:50%;" />
</div>


##### 检测超时任务

在这一环节，调度器会迭代地检查存有睡眠状态的uthread的红黑树，一旦发现有到时的任务，就会马上将其从树上取下并执行。每一个处于睡眠状态的uthread会维护一个醒来的时刻，红黑树以这个变量作为排序的关键字。因此，调度器会从醒来时刻最早的协程开始处理，直到将红黑树上所有的到时任务都处理完毕。

##### 执行就绪任务 

在执行就绪队列中的任务时，调度器只会执行此次遍历开始时所有位于队列中的任务，在执行期间新插入的任务不会在这此次调度循环中被执行。值得一提的是，如果因为读磁盘等阻塞系统调用而发生了p的转移，那么此时调度器不绑定任何p任务集合。于是在执行完阻塞系统调用所在的协程后，执行环节会当即结束。并且此后的监听socket以及处理就绪事件环节都不会被执行，调度器没有其他任何任务。对于没有任务的空调度器，目前我们的处置方式是直接销毁线程。

##### 监听socket文件描述符

调度器采用epoll机制为各个被“阻塞”的协程监听socket的文件描述符。相较于select和poll，epoll机制无需将要被监听的文件描述符和期望事件的信息反复写入内核，而且在调用epoll_wait之前就已经由内核开始进行监听。当调度器执行epoll_wait时，只需要遍历一遍位于内核中的就绪事件的双向链表即可。epoll的这些特性极大地提高了调度器的监听效率。如果调度器执行到epoll_wait时内核还未监听到任何就绪事件，那么调度器会被阻塞，直到监听到相关的就绪事件或者监听超时。此处超时时间的设置会考虑到调度器中正在睡眠的那些协程。如果存在处于睡眠状态的协程，那么epoll_wait监听的超时时刻不应改超过最近的休眠协程被唤醒的时刻。

##### 处理就绪事件

当就绪事件发生时，epoll_wait会立即返回，并将就绪事件放在sched->p->eventlist中。对于eventlist中的每个事件，我们通过FD_KEY(fd，e)作为key，将它从waitting tree上移除；同时，如果它是UT_ST_SLEEPING状态，将它也从sleeping tree上移除，这两棵tree使用的数据结构都是红黑树，插入、查找和删除都具有O(logn)的高效性能。除此之外，对于每个事件，我们还需要从poll_fd中取消监听，并修改对应的状态。此时，就调用_uthread_resume(ut) 恢复执行该ut了。
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(4).png" width=25% height=25% style="zoom:25%;" />
</div>


#### 2.2.3 socket I/O接口的实现

我们首先修改sock_fd为非阻塞的。这样对于网络编程相关的函数，如accept、connect、read和write等，都需要进行返回值判断，并作出相应的处理。通常分为几大类：
- 如果是暂时未就绪的，就注册监听事件，并放到sleep tree和waitting tree，再主动yield让出。因此，当事件就绪、或者超时发生时，该函数得以继续执行；
- 如果是成功返回，例如连接成功、send n bytes、recv n bytes等，直接将该结果返回，供上层使用；
- 如果返回值是异常值，则进行出错处理；
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(5).png" width=50% height=50% style="zoom:23%;" />
</div>


#### 2.2.4 阻塞系统调用

在线程模型下，当一个线程试图读取磁盘文件时，或者在终端这样需要与用户交互的场景下，通常不能在执行系统调用时马上读取到相应的数据，因而线程会被阻塞，直到可以进行读操作才会被唤醒。在线程-协程模型下，线程级别的阻塞是不可以轻易发生的，否则位于同一线程上的所有其它协程全部都会被阻塞而无法得到执行。  

对于socket I/O，我们已经通过将socket的文件描述符修改为非阻塞，然后在描述符未就绪时通过调用框架内部用于“阻塞”协程的函数实现协程级别的阻塞。事实上，对于读取终端（shell交互），也可以使用这样的方式。然而，对于读磁盘这样的特殊情形，文件描述符不会像socket I/O那样出现未就绪而标记错误号为EAGAIN的情况。若操作系统发现内核页缓冲区没有预读入的数据，则会将发起请求的线程阻塞，然后将数据从磁盘读取到内核页缓冲。也就是说，我们无法避免因为磁盘I/O而出现长时间的线程阻塞。对于这样几乎确定会长时间阻塞线程的系统调用，我们将线程上的p任务集合整体转移到其它的线程上，从而避免了同一线程上的其他任务无法执行。p在线程之间的转移示意图如下：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(6).png" width=50% height=60% style="zoom:85%;" />
</div>


#### 2.2.5 抢占
###### Linux内核抢占机制

Linux内核（2.6版本）加入了内核抢占机制。内核抢占指用户程序在执行系统调用期间可以被抢占，该进程暂时挂起，使新唤醒的高优先级进程能够运行。抢占式调度分为两步。第一步在current进程设置需要重新调度的标志TIF_NEED_RESCHED，第二步在某些特定的时机，检测是否设置了TIF_NEED_RESCHED标志，若设置了就调用 schedule函数发生进程调度。
![avatar](https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(7).png)

###### Go的抢占机制

现代操作系统的调度器多为抢占式调度，其实现方式通过硬件中断来支持线程的切换，进而能安全的保存运行上下文。在 Go 运行时实现抢占式调度同样也可以使用类似的方式，通过向线程发送系统信号的方式来中断 M 的执行，进而达到抢占的目的  

在1.14之前，抢占的一种方式与运行时系统监控有关，监控循环会将发生阻塞的Goroutine抢占，解绑 P 与 M，从而让其他的线程能够获得P 继续执行其他的 Goroutine。通过由sysmon线程初始化，该线程专门用于监控包括长时间运行的协程在内的运行时。当某个协程被检测到运行超过 10ms 后，sysmon向当前的线程发出一个抢占信号。  

起初runtime.main会创建一个额外的M运行sysmon函数,抢占就是在sysmon中实现的. sysmon会进入一个无限循环,第一轮会休眠20us,之后每次休眠时间倍增,最大不会超过10ms.sysmon会调用retake()函数，retake()函数会遍历所有的P，如果一个P处于Psyscall状态，会被调用handoffp来解绑MP关系。 如果处于Prunning执行状态，一直执行某个G且已经连续执行了 > 10ms的时间，就会被抢占。
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(8).png" width=50% height=60% style="zoom:85%;" />
</div>


###### uthread抢占机制设计

我们整体的抢占调度设计方案也是借鉴于Go中的抢占调度方式，在初始化系统时会创建一个监控线程用于监控进程中所有uthread的运行情况。我们在监控线程中会运行一个时间轮定时器，所有的uthread在调度器sched上开始运行时会在该定时器上进行注册。时间轮进行轮转，当发现当前时刻有协程运行时间达到阈值(10ms)时，我们就会给该协程所在线程发送信号通知该其需要执行流转动作。同时，为了区别主动yield让出的uthread和计算密集型uthread（运行时间到达10ms，定时器时间到达），我们在uthread结构体中加入is_waiting_yield_signal参数用来标识。在uthread初始化时该标志位为1。若uthread主动让出，我们在uthread_yield()中将is_waiting_yield_signal置为0。在uthread_resume()中再置回1。这样我们在时间轮tick函数中，只要筛选给is_waiting_yield_signal=1的uthread发信号即可。  

我们使用如下结构来模拟时间轮定时器的功能。轮中的实线指针指向轮子上的一个槽（slot），它以恒定的速度顺时针转动，每转动一步就指向下一个槽，每次转动称为一个滴答（tick）。一个滴答的时间称为是间轮的槽间隔si（slot interval），它实际上就是心跳时间。该轮共有N个槽，因此它运转一周的时间是N×si 。每个槽指向一条定时器链表，每条链表上的定时器具有相同的特性：它们的定时时间相差N×si的整数倍。时间轮正是利用这个关系将定时器散列到不同的链表中。假如现在指针指向槽cs，我们要添加一个定时时间为ti的定时器，则该定时器将被插入ts（timer slot）对应的链表中：ts = (cs + (ti / si)) %N。
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(9).png" width=50% height=60% style="zoom:85%;" />
</div>


在上图中，定时器中expire表示到期时间，rotation表示节点在时间轮转了几圈后才到期。当当前时间指针指向某个bucket时，不能像简单时间轮那样直接对bucket下的所有节点执行超时动作，而是需要对链表中节点遍历一遍，判断轮子转动的次数是否等于节点中的rotation值，当两者相等时，方可执行超时操作。  

在时间轮实现中，我们首先定义定时器任务节点，结构体中有三个成员。rotation表示节点在时间轮转了几圈后才到期，ut是节点绑定的协程结构体指针，每个节点有一个next指针。在此基础上，时间轮也很容易就能表示出来。其中TIME_WHEEL_SIZE表示槽slot的个数，current表明现在时间轮执行到了哪一个槽位。  

每个uthread协程注册插入到时间轮的逻辑设计如下：我们在插入时需要两个参数，第一个参数len表示该uthread需要执行的时间，第二个参数ut表明所绑定的协程结构体指针。我们只需要知道len和时间轮当前的槽位即可确定该节点在时间轮上需要挂载的位置pos。找到槽位后，在插入链表时我们采用头插法插入到链表头部即可。这样能使插入的时间复杂度控制在O(1)。  

在设计完时间轮的相关结构后，我们设计了相应的控制逻辑。整体的逻辑流程图如下：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(10).png" width=65% height=75% style="zoom:85%;" />
</div>


对于该时间轮，我们需要在系统初始化时就进行创建操作。具体创建操作我们封装在一个函数create_timewheel()中。我们使用了linux内核自带的setitimer用来实现延时和定时的功能。其中的new_value参数用来对计时器进行设置，it_interval为计时间隔，it_value为延时时长。setitimer工作机制是，先对it_value倒计时，当it_value为零时触发信号，然后重置为it_interval，继续对it_value倒计时，一直这样循环下去。在设置完后系统内核会定时给进程发送SIGALRM信号来通知进程执行相关操作。在本项目中，每次SIGALRM信号到达时执行我们给进程绑定的tick函数。设置定时器时间阈值为10ms，如果计算密集型协程执行时间超过该阈值，监控线程则给该协程发送信号使其yield让出。  

其中SIGALRM信号绑定的tick函数用来执行时间轮的正常轮转。如果发现此时时间轮上有注册的协程事件到达，我们就从相应的槽位上取出链表，依次“执行”。注意，此时并不能立即执行协程任务，我们是通过向协程所在的线程发送信号，通知该协程执行所需要做的动作(yield)来实现。发送信号的实现通过pthread_kill来向指定线程发送信号这个API来实现。为了区别主动yield让出的uthread和计算密集型uthread（运行时间到达10ms，定时器时间到达），我们引入了is_waiting_yield_signal标志位。

#### 2.2.6 hook

Linux的hook通过动态链接的方式，当共享对象被load进来的时候，它的符号表会被合并到进程的全局符号表中（这里说的全局符号表并不是指里面的符号全部是全局符号，而是指这是一个汇总的符号表），当一个符号需要加入全局符号表时，如果相同的符号名已经存在，则后面加入的符号被忽略。  

由于glibc是c/cpp程序的运行库，因此它是最后以动态链接的形式链接进来的，我们可以保证其肯定是最后加入全局符号表的，由于全局符号介入机制，glibc中的相关socket函数符号被忽略了，也因此只要最终的可执行文件链接了hook函数，就可以基本保证相关的socket函数被hook掉了。


## 三.功能描述

### 3.1 协程管理

**pthread_create**

```C
int pthread_create(pthread_t *tidp, const pthread_attr_t *attr, void *(*start_rtn)(void*), void *arg);
```
创建一个协程。在用户第一次调用此函数时，会先进行整个运行时系统的初始化，并将main函数的剩余代码封装进一个协程的执行流中，然后为用户指定的函数创建一个协程。  

参数：
- tidp (pthread_t*）-- 此处的pthread_t仍为posix线程下的类型名，hook之后的pthread_create会将其用于存储被创建的协程的结构体地址，即struct uthread* 指针的变量值。
- attr (const pthread_attr_t*) -- attr仍然为posix线程下，但hook后的pthread_create不会处理这个参数。
- start_rtn (void\*(\*)(void*)) -- 为创建的协程绑定的函数。
- arg (void *) -- 为协程所绑定的函数传入的参数。  

返回值：
- 成功返回0，并使tidp存储被创建的协程结构体的地址数值。
- 失败返回-1，并标志errno表示错误的原因。

**pthread_join**

```C
int pthread_join(pthread_t thread, void **retval);
```
连接到另一个协程。一个协程只有被标记为detached，或者被其它协程连接，该协程退出后才会完全释放自身所占有的资源，否则该协程的id无法被新创建的协程使用。 

参数：
- thread (pthread_t) -- 将要被连接的协程结构体的地址值，在hook后的pthread_join中，这个参数会被先强制转换程struct uthread*，然后再调用框架内部的连接函数。
- retval (void **) -- 用于存储被连接的协程在结束时的返回信息。
 

返回值：
- 成功返回0，retval指向被连接的协程的返回值。
- 连接超时返回1，默认时长限制为10秒。
- 失败返回-1，并标记errno表示错误的原因。

**pthread_exit**

```C
void pthread_exit(void *retval);
```
退出协程。若retval不为空，会向连接到自己的协程返回一些信息，这个信息不应该位于局部存储空间中。

参数：
- retval (void*) -- 用于将信息返回给连接到自己的协程。


**pthread_self**

```C
pthread_t pthread_self(void);
```
返回协程的结构体地址。pthread_t 为posix类型名，用于返回当前协程的结构体地址的数值。

参数：
- 无

返回值：
- 返回协程的结构体地址的数值。

**pthread_detach**

```C
int pthread_detach(pthread_t thread); 
```
将协程标记为分离状态。被detach的协程不允许被其它协程连接，退出后可以直接由运行时回收该协程所占有的全部资源。

参数：
- thread (pthread_t) -- posix线程类型名，在hook后的函数内部会被抢占转换为协程结构体的地址。

返回值：
- 目前仅返回0表示成功。


### 3.2 socket I/O

**socket**

```C
int socket(int domain, int type, int protocol);
```
创建一个新的socket，并设置socket的文件描述符为non-blocking。

参数：
- domain(int) -- 设置用于通信的协议族。
- type(int) -- 设置socket类型。
- protocol(int) -- 通常赋值为0，由系统自动选择。

返回值：
- 成功时返回非负数的socket描述符。
- 失败返回-1。


**accept**

```C
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
```
uthread版本的accept，被TCP类型的服务端调用，返回一个建立成功的连接
若没有收到连接，会“阻塞”该uthread，并注册一个读事件
uthread_accept，timeout为无穷大，永远不会超时唤醒。

参数：
- sockfd(int) -- 为socket文件描述符。
- addr(struct sockaddr *) -- 用于保存发起连接请求的客户端的协议地址。
- addrlen(socklen_t) -- 设置输入时缓冲区的长度，通常设置为sizeof(addr)。


返回值：
- 成功时返回已接受的socket的文件描述度，非负数。
- 失败时返回-1，并设置错误码errno。


**connect**

```C
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```
uthread版本的connect，客户端调用，用来与服务端建立连接；
若没有建立连接，会“阻塞”该uthread，并注册一个写事件，且设置超时事件，默认是1000ms，因此connect可能会出现超时的情况。


参数：
- sockfd(int) -- 为socket文件描述符
- addr(struct sockaddr *) -- 用于保存发起连接请求的客户端的协议地址。
- addrlen(socklen_t) -- 设置输入时缓冲区的长度，通常设置为sizeof(addr)。


返回值：
- 成功时返回已接受的socket的文件描述度，非负数。
- 失败时返回-1，并设置错误码errno。

**close**

```C
int close(int fd);
```
uthread版本的close，唤醒等待这个fd的ut，并将ut的状态设置为UT_ST_FDEOF，再关闭一个文件描述符。

参数：
- fd(int) -- 待关闭的文件描述符。

返回值：
- 成功时返回0。
- 失败时返回-1，并设置错误码errno。

**read**

```C
ssize_t  read(int fd, void *buf, size_t length);
```
uthread版本的read，尝试从文件描述符fd中，读取length字节的数据，到buf缓冲区中非阻塞模式，默认的timeout为1000ms。

参数：
- fd(int) -- 待读取的文件描述符。
- buf(void*) -- 字符缓冲区，用来存放读取到的数据。
- length(size_t) -- 待读取的字节数。


返回值：
- 成功时，返回读到的字节数（0意味着读到了EOF）
- 失败时返回-1，并设置错误码errno。


**recv、recvfrom、recvmsg**

```C
ssize_t recv(int fd, void *buf, size_t length, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);
size_t recvfrom(int fd, void *buf, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
```
uthread版本的recv/recvmsg/recvfrom，都是用来从socket中接收数据。它与read的唯一区别是flags参数。非阻塞模式，默认的timeout为1000ms。


参数：
- fd(int) -- 待接收的socket文件描述符。
- buf(void *) -- 字符缓冲区，用来存放接收到的数据。
- length(size_t) -- 待接收的字节数。
- flags(int) -- 一系列的参数设置，一般情况下置为0。
- address(struct sockaddr *) -- 用于保存发起连接请求的客户端的协议地址。
- address_len(socklen_t) -- 设置输入时缓冲区的长度，通常设置为sizeof(addr)。



返回值：
- 成功时返回接收到的字节数。
- 失败时返回-1，并将errno设置为EAGAIN或EWOULDBLOCK。


**write**

```C
ssize_t write(int fd, const void *buf, size_t length);
```
uthread版本的write，从文件描述符fd的buf位置开始，写入length长度的字节。


参数：
- fd(int) -- 待写入的文件描述符。
- buf(const void *) -- 设置字符缓冲区的起始位置。
- length(size_t) -- 写入的字节数。

返回值：
- 成功时返回写入的字节数。
- 失败时返回-1，并设置错误码errno。


**send、sendmsg、sendto**

```C
ssize_t send(int fd, const void *buf, size_t length, int flags);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t sendto(int fd, const void *buf, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
```
uthread版本的send/sendto/senfmsg，用于向socket连接发送信息。非阻塞模式，默认的timeout为1000ms。

参数：
- fd(int) -- 待发送的文件描述符。
- buf(const void *) -- 设置字符缓冲区的起始位置。
- message(const struct msghdr *) -- 待发送的字符串。
- flags(int) -- 一系列的参数设置，一般情况下置为0。
- address(struct sockaddr *) -- 用于保存发起连接请求的客户端的协议地址。
- address_len(socklen_t) -- 设置输入时缓冲区的长度，通常设置为sizeof(addr)。

返回值：
- 成功时返回已发送的字节数。
- 失败时返回-1，并保持文件指针不变。


**writev**

```
ssize_t writev(int fd, struct iovec *iov, int iovcnt);
```
uthread版本的writev，用于写入一个数组的数据。

参数：
- fd(int) -- 待写入的文件描述符。
- iov(struct iovec *) -- 存放待写入的数据数组。
- iovcnt(int) -- iov数组中元素的个数。

返回值：
- 成功时返回成功写入的字节数。
- 失败时返回-1，并设置错误码errno。 


## 四.操作指引

```
1. 向库配置文件/etc/ld.so.conf.d/usr-libs.conf中，写入库文件所在目录/usr/local/lib  
vim /etc/ld.so.conf.d/usr-libs.conf    
/usr/local/lib  
2. 更新/etc/ld.so.cache文件
执行ldconfig 
3. 引入uthread.h
```



## 五.效果展示

测试环境：Linux X86_64

1. test_uthread.c  

该测试文件主要测试uthread的基本使用，定义三个协程(已hook pthread)，并分别绑定执行函数进行交替打印输出1~99。

```C
int main(int argc, char **argv) {
    enable_hook();   
    pthread_t p1,p2,p3;
    pthread_create(&p1,NULL,a,NULL);
    pthread_create(&p2,NULL,b,NULL);
    pthread_create(&p3,NULL,c,NULL)
    printf("main is running...\n");
    printf("main is exiting...\n");
    main_end();
}
```

效果展示（由于打印输出很长，只截取部分输出）：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(11).png" width=50% height=60% style="zoom:85%;" />
</div>


2. test_join_exit.c  

该测试文件主要用于测试uthread的join和exit，以及其它一些基本的协程管理接口，首先在main函数中执行pthread_create(已hook), 创建两个uthread，分别绑定a，b函数，主线程main中执行pthread_join(已hook)分别等待两个uthread的执行完毕，最后主线程main退出。执行pthread_exit的函数会返回传递一些消息给join到它的main协程。

```C
int main() {
    enable_hook();
    printf("main is running.\n");
    
    pthread_t p, p2;
    pthread_create(&p, NULL, a, NULL);
    pthread_create(&p2, NULL, b, NULL);

    void *retval = NULL;
    printf("main about to join a.\n");
    pthread_join(p, &retval);
    printf("main waken up.\n");
    printf("msg returned by a: %s", (char *)retval);
    
    void *retval2 = NULL;
    printf("main about to join b.\n");
    pthread_join(p2, retval2);
    printf("main waken up.\n");
    printf("main is existing.\n\n");   
    uthread_main_end();   
}
```
效果展示：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(12).png" width=50% height=60% style="zoom:85%;" />
</div>


3. test_socket_io.c  
测试socket io相关函数，包括socket，connect，accapt，read和write等，这些接口都是非阻塞的，当监听到对应事件就绪时才会回来继续执行。  
我们在一个程序中创建两个uthread，分别执行客户端程序和服务端程序，客户端接收键盘的输入，发送数据到socket fd；服务端从socket fd读取出数据，并打印出来。

```
int main() {  
    enable_hook();

    pthread_t server_p, client_p;
    pthread_create(&server_p,NULL, myserver, NULL);
    sleep(1);
    pthread_create(&client_p,NULL, myclient, NULL);

    main_end();
}
```
效果展示：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(13).png" width=50% height=60% style="zoom:85%;" />
</div>


4. test_timer.c  

该文件主要测试uthread调度器的抢占调度，在初始化系统时会创建一个监控线程用于监控进程中所有uthread的运行情况。我们在监控线程中会运行一个时间轮定时器，所有uthread在调度器sched上开始运行时会在该定时器上进行注册。时间轮进行轮转，当发现当前时刻有协程运行时间达到阈值(10ms)时，我们给该协程所在线程发送信号通知该其需要执行流转动作。  
首先创建两个uthread，分别执行a，b函数，函数体中a和b分别进行打印输出并睡眠。可以发现ut1，ut2和主协程的打印语句是间隔输出的，这说明调度器实现抢占，每10ms通知协程执行了yield让出操作。时间轮定时抢占模块功能基本实现。

```C
void a (void *x) {
    for (;;){
        printf("a is running\n");
        sleep(1000);
    } 
}
void b (void *x) {
    for (;;) {
        printf("b is running\n");
        sleep(1000);
    }
}
int main (int argc, char **argv) {
    struct uthread *ut = NULL;
    uthread_create(&ut, a, NULL);
    struct uthread *ut2 = NULL;
    uthread_create(&ut2, b, NULL);
    struct uthread *ut3 = NULL; 
    uthread_create(&ut3, c, NULL);
    for(;;) {
        printf("main is running...\n");
        sleep(1000);
    }
    main_end();   // 将main协程删除，防止main协程先结束导致整个进程结束
}
```
效果展示：
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/image%20(14).png" width=50% height=60% style="zoom:85%;" />
</div>


5. test_disk_io.c
 
对于阻塞系统调用场景下p的转移，我们不直接测试对磁盘的读写，而是通过读操作阻塞在终端这一情形上来体现p的转移。我们同时创建两个协程a和b，a调用接口pthread_disk_read和pthread_disk_write进行和用户的交互，b持续地进行执行间隔一段时间之后地输出。我们预期a在执行pthread_disk_read的时候，会将包含b的任务集合p转移到另一个线程上去，测试用例运行时应当会并发地进行与用户的交互任务和打印任务。测试用例的关键代码如下：
```C
void *
a(void *arg) {
    char c;
    // 输入ctrl + d退出循环
    while (pthread_disk_read(STDIN_FILENO, &c, 1)) {
        pthread_disk_write(STDOUT_FILENO, &c, 1);
    }
}

void *
b(void *x) {
    int j = 0;
    for (long i = 0; i < 10000000000000; ++i) {
        if (i % 500000000 == 0)      // 方便效果演示
            printf("uthread b: j is %d\n", j++);
    }
}

int
main(int argc, char **argv) {
    enable_hook();

    pthread_t p, p2;
    pthread_create(&p, NULL, a, NULL);
    pthread_create(&p2, NULL, b, NULL);

    main_end();
}
```
测试效果如下图所示
<div align = center>
    <img src="https://cdn.jsdelivr.net/gh/growvv/image-bed//mac-m1/20210331210546.png" width=50% height=60% style="zoom:85%;" />
</div>
