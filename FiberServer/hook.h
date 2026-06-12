#pragma once
#include<fcntl.h>
#include<sys/socket.h>
#include<unistd.h>
#include<stdint.h>
#include<sys/types.h>
#include<sys/ioctl.h>

//hook 实现对io和一些底层函数的"阻拦" 用户调用sleep read 实际调用的重写后的函数
//这样就实现了同步函数写异步了
namespace FiberServer{
    bool is_hook_enable();//当前线程是否hook
    void set_hook_enable(bool flag);//设置当前线程hook状态
}
extern "C"//这里写是因为hook要想阻断系统调用就必须其和系统函数一样的名字
//但是cpp里函数在链接器中不等于其原来的名字 其真实名字在原名字中加有参数名(为了实现重载)
//extern指定函数名用C的规则 即真实名为原来名 这样调用sleep就为真实名了
{
//函数指针的命名 c++的看起来比c的typedef舒服多了

//下面第一个是声明函数指针 第二个声明变量用来存放系统函数 这样在重写的函数里可以调用系统函数
//sleep
using sleep_fun=unsigned int (*)(unsigned int seconds);//typedef unsigned int (*sleep_fun)(unsigned int seconds);
extern sleep_fun sleep_f;//声明变量

using usleep_fun =int (*)(useconds_t usec);
extern usleep_fun usleep_f;

using nanosleep_fun =int (*)(const struct timespec *rea,struct timespec *rem);
extern nanosleep_fun nanosleep_f;

using socket_fun=int (*)(int domain,int type,int protocol);
extern socket_fun socket_f;

using connect_fun =int (*)(int sockfd,const struct sockaddr *addr,socklen_t addrlen);
extern connect_fun connect_f;

using accept_fun =int (*)(int s,struct sockaddr *addr,socklen_t *addrlen);
extern accept_fun accept_f;

using read_fun =ssize_t (*)(int fd,void *buf,size_t count);
extern read_fun read_f;

using readv_fun =ssize_t(*)(int fd,const struct iovec *iov,int iovcnt);
extern readv_fun readv_f;

using recv_fun= ssize_t (*)(int sockfd, void *buf, size_t len, int flags);
extern recv_fun recv_f;

using recvfrom_fun =ssize_t (*)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_fun recvfrom_f;

using recvmsg_fun=ssize_t (*)(int sockfd,struct msghdr *msg,int flags);
extern recvmsg_fun recvmsg_f;

using write_fun=ssize_t(*)(int fd,const void *buf,size_t count);
extern write_fun write_f;

using writev_fun=ssize_t(*)(int fd,const struct iovec *iov,int iovcnt);
extern writev_fun writev_f;

using send_fun=ssize_t (*)(int s, const void *msg, size_t len, int flags);
extern send_fun send_f;

using sendto_fun=ssize_t (*)(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
extern sendto_fun sendto_f;

using sendmsg_fun= ssize_t (*)(int s, const struct msghdr *msg, int flags);
extern sendmsg_fun sendmsg_f;

using close_fun= int (*)(int fd);
extern close_fun close_f;

using fcntl_fun= int (*)(int fd,int cmd, ... /* arg */ );
extern fcntl_fun fcntl_f;

using ioctl_fun=int(*)(int d,unsigned long int request,...);
extern ioctl_fun ioctl_f;

using setsockopt_fun =int (*)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern setsockopt_fun setsockopt_f;

using getsockopt_fun =int (*)(int sockfd, int level, int optname, void *optval, socklen_t* optlen);
extern getsockopt_fun getsockopt_f;


extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms);

}