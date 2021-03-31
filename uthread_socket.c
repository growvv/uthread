#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>     // read
#include <dlfcn.h>
#include <sys/uio.h>    //writev
#include "uthread_inner.h"

#define FLAG | MSG_NOSIGNAL

int uthread_socket(int domain, int type, int protocol) {
    int sock_fd;
    int (*sys_socket)(int domain, int type, int protocol) = dlsym(RTLD_NEXT, "socket");
    assert((sock_fd = sys_socket(domain, type, protocol)) != -1);
    // assert((sock_fd = socket(domain, type, protocol)) != -1);
    assert(fcntl(sock_fd, F_SETFL, O_NONBLOCK) != -1);
    return sock_fd;
}

// accept永不超时
int uthread_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int res;
    struct uthread *ut = _sched_get()->cur_uthread;
    int (*sys_accept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen) = dlsym(RTLD_NEXT, "accept");
    // printf("uthread_accept id: %d\n", (int)ut->id);

    while (1) {
        res = sys_accept(sockfd, addr, addrlen);
        // printf("uthread_accept res: %d\n", res);
        if (res == -1) {
            // 若现在没有收到连接、若无法继续创建一个新的fd，“阻塞”协程，并注册一个读事件
            if (errno == EAGAIN || errno == ENFILE || errno == EMFILE) {
                _register_event(ut, sockfd, UT_EVENT_RD, 0);    // timeout为0，永远不会超时唤醒
                continue;
            } else if (errno == ECONNABORTED) {
                perror("A connection has been aborted");
                continue;
            } else {
                perror("Cannot accept connection");
                return -1;
            }
        }    
        return res; // 创建套接字成功
    }
}

// connect有可能超时
int uthread_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int res;
    struct uthread *ut = _sched_get()->cur_uthread;
    int (*sys_connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen) = dlsym(RTLD_NEXT, "connect");
    while (1) {
        res = sys_connect(sockfd, addr, addrlen);
        // printf("connect: %d\n", res);
        if (res == -1) {
            if (errno == EAGAIN || errno == EINPROGRESS) {
                // printf("error: %d\n", errno);
                _register_event(ut, sockfd, UT_EVENT_WR, 0); // 要设置超时时间 
                if (ut->status & BIT(UT_ST_EXPIRED)) {  // 如果connect超时
                    errno = ETIMEDOUT;
                    // printf("连接超时\n");
                    return -1;
                }
                continue;
            }
            return -1;
        } else
            return res;
    }
}


// 用于封装read族和recv族的接口                        
ssize_t uthread_read(int fd, void *buf, size_t length) {
    ssize_t ret = 0;                                       
    struct uthread *ut = _sched_get()->cur_uthread;
    int (*sys_read)(int fd, void *buf, size_t length) = dlsym(RTLD_NEXT, "read");
    while (1) {     
        // printf("非阻塞读\n");
        if (ut->status & BIT(UT_ST_FDEOF))                   
            return (-1);                                    
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
        ret = sys_read(fd, buf, length);                 
        // printf("ret: %d\n", (int)ret);
        if (ret == -1 && errno != EAGAIN) {
            return (-1);                                    
        }
        if ((ret == -1 && errno == EAGAIN)) {               
            // printf("怎么才能触发您啊？？\n");
            _register_event(ut, fd, UT_EVENT_RD, 1000);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
        if (ret >= 0)                                       
            return (ret);                                   
    }                                                       
}  

ssize_t uthread_recv(int fd, void *buf, size_t length, int flags) {
    ssize_t ret = 0;                                       
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {     
        if (ut->status & BIT(UT_ST_FDEOF))                   
            return (-1);                                    
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
        ret =  recv(fd, buf, length, flags FLAG);                
        if (ret == -1 && errno != EAGAIN) {
            return (-1);                                    
        }
        if ((ret == -1 && errno == EAGAIN)) {               
            _register_event(ut, fd, UT_EVENT_RD, 0);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
        if (ret >= 0)                                       
            return (ret);                                 
    }                                                     
}  
ssize_t uthread_recvmsg(int fd, struct msghdr *message, int flags) {
    ssize_t ret = 0;                                       
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {     
        if (ut->status & BIT(UT_ST_FDEOF))                   
            return (-1);                                    
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
        ret =  recvmsg(fd, message, flags FLAG);            
        if (ret == -1 && errno != EAGAIN) {
            return (-1);                                    
        }
        if ((ret == -1 && errno == EAGAIN)) {               
            _register_event(ut, fd, UT_EVENT_RD, 0);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
        if (ret >= 0)                                       
            return (ret);                                   
    }                                                       
}  
ssize_t uthread_recvfrom(int fd, void *buf, size_t length, int flags,
        struct sockaddr *address, socklen_t *address_len) {
    ssize_t ret = 0;                                       
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {     
        if (ut->status & BIT(UT_ST_FDEOF))                   
            return (-1);                                    
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
        ret =  recvfrom(fd, buf, length, flags FLAG, address, address_len);           
        if (ret == -1 && errno != EAGAIN) {
            return (-1);                                    
        }
        if ((ret == -1 && errno == EAGAIN)) {               
            _register_event(ut, fd, UT_EVENT_RD, 0);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
        if (ret >= 0)                                       
            return (ret);                                   
    }                                                       
} 

// 用于封装read_exact族和recv_exact族的接口   
ssize_t uthread_recv_exact(int fd, void *buf, size_t length, int flags) {                                                         
    ssize_t ret = 0;                                        
    ssize_t recvd = 0;                                      
   struct uthread *ut = _sched_get()->cur_uthread;   
                                                            
    while (recvd != length) {                               
        if (ut->status & BIT(UT_ST_FDEOF))                  
            return (-1);                                    

        ret = recv(fd, buf + recvd, length - recvd, flags FLAG);   
        if (ret == 0)                                       
            return (recvd);                                 
        if (ret > 0)                                        
            recvd += ret;                                   
        if (ret == -1 && errno != EAGAIN)                   
            return (-1);                                    
        if ((ret == -1 && errno == EAGAIN)) {               
            _register_event(ut, fd, UT_EVENT_RD, 0);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
    }                                                       
    return (recvd);                                         
}                                                           
ssize_t uthread_read_exact(int fd, void *buf, size_t length) {                                                         
    ssize_t ret = 0;                                        
    ssize_t recvd = 0;                                      
   struct uthread *ut = _sched_get()->cur_uthread;   
                                                            
    while (recvd != length) {                               
        if (ut->status & BIT(UT_ST_FDEOF))                  
            return (-1);                                    

        ret = read(fd, buf + recvd, length - recvd);        
        if (ret == 0)                                       
            return (recvd);                                 
        if (ret > 0)                                        
            recvd += ret;                                   
        if (ret == -1 && errno != EAGAIN)                   
            return (-1);                                    
        if ((ret == -1 && errno == EAGAIN)) {               
            _register_event(ut, fd, UT_EVENT_RD, 0);  
            if (ut->status & BIT(UT_ST_EXPIRED))             
                return (-2);                                
        }                                                   
    }                                                       
    return (recvd);                                         
}                                                           

// 用于封装write和send族接口
ssize_t uthread_write(int fd, const void *buf, size_t length)  {                                                        
    ssize_t ret = 0;
    ssize_t sent = 0;
    struct uthread *ut = _sched_get()->cur_uthread;
    int (*sys_write)(int fd, void *buf, size_t length) = dlsym(RTLD_NEXT, "write");
    while (sent != length) {
        if (ut->status & BIT(UT_ST_FDEOF))
            return (-1);
        ret = sys_write(fd, ((char *)buf) + sent, length - sent);                                         
        if (ret == 0)
            return (sent);
        if (ret > 0)
            sent += ret;
        if (ret == -1 && errno != EAGAIN)
            return (-1);
        if (ret == -1 && errno == EAGAIN)
            _register_event(ut, fd, UT_EVENT_WR, 0);
    } 
    return (sent);
}  
ssize_t uthread_send(int fd, const void *buf, size_t length, int flags)  {                                                        
    ssize_t ret = 0;
    ssize_t sent = 0;
    struct uthread *ut = _sched_get()->cur_uthread;
    while (sent != length) {
        if (ut->status & BIT(UT_ST_FDEOF))
            return (-1);
        ret = send(fd, ((char *)buf) + sent, length - sent, flags FLAG);                                        
        if (ret == 0)
            return (sent);
        if (ret > 0)
            sent += ret;
        if (ret == -1 && errno != EAGAIN)
            return (-1);
        if (ret == -1 && errno == EAGAIN)
            _register_event(ut, fd, UT_EVENT_WR, 0);
    } 
    return (sent);
}  

// 用于封装sendmsg和sendto接口
ssize_t uthread_sendmsg(int fd, const struct msghdr *message, int flags) {                                                         
    ssize_t ret = 0;                                        
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {                                             
        if (ut->status & BIT(UT_ST_FDEOF))                  
            return (-1);                                    
        ret = sendmsg(fd, message, flags FLAG);                                            
        if (ret >= 0)                                       
            return (ret);                                   
        if (ret == -1 && errno != EAGAIN)                   
            return (-1);                                    
        if (ret == -1 && errno == EAGAIN)                   
           _register_event(ut, fd, UT_EVENT_WR, 0);         
    }                                                       
}                                                           
ssize_t uthread_sendto(int fd, const void *buf, size_t length, int flags,
        const struct sockaddr *dest_addr, socklen_t dest_len) {                                                         
    ssize_t ret = 0;                                        
    struct uthread *ut = _sched_get()->cur_uthread;
    while (1) {                                             
        if (ut->status & BIT(UT_ST_FDEOF))                  
            return (-1);                                    
        ret = sendto(fd, buf, length, flags FLAG, dest_addr, dest_len);                                            
        if (ret >= 0)                                       
            return (ret);                                   
        if (ret == -1 && errno != EAGAIN)                   
            return (-1);                                    
        if (ret == -1 && errno == EAGAIN)                   
           _register_event(ut, fd, UT_EVENT_WR, 0);         
    }                                                       
}  


// 封装writev，一次写多个fd（socket）
ssize_t uthread_writev(int fd, struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    int iov_index = 0;
    struct uthread *ut = _sched_get()->cur_uthread;

    do {
        ssize_t n = writev(fd, iov + iov_index, iovcnt - iov_index);
        if (n > 0) {
            int i = 0;
            total += n;
            for (i = iov_index; i < iovcnt && n > 0; i++) {
                if (n < iov[i].iov_len) {
                    iov[i].iov_base += n;
                    iov[i].iov_len -= n;
                    n = 0;
                } else {
                    n -= iov[i].iov_len;
                    iov_index++;
                }
            }
        } else if (-1 == n && EAGAIN == errno) {
           _register_event(ut, fd, UT_EVENT_WR, 0);
        } else {
            return (n);
        }
    } while (iov_index < iovcnt);

    return (total);
}
