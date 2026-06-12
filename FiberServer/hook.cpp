#include "hook.h"
#include <dlfcn.h>
#include <utility>
#include <stdarg.h>
#include<stdint.h>
#include "base/log.h"
#include "base/macro.h"
#include "base/config.h"
#include "fd_manager.h"
#include "iomanager.h"
FiberServer::Logger::ptr g_logger=FIBER_LOG_NAME("system");
namespace FiberServer{
    static ConfigVar<int>::ptr g_tcp_connect_timeout = Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");
    static thread_local bool t_hook_enable = false;//用来判断该线程是否要hook 默认不用
    #define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

    
void hook_init(){
    static bool is_inited=false;
    if(is_inited){
        return;
    }
#define XX(name) name ## _f =(name ## _fun)dlsym(RTLD_NEXT,#name);//给所有的函数指针赋值 用宏简化 相当于sleep_f=(sleep_f*)dlsym(RTLD_NEXT,sleep)保存系统函数指针
 HOOK_FUN(XX);
#undef XX
}
static uint64_t s_connect_timeout = -1;
struct _HookIniter{
    _HookIniter(){
        hook_init();
        s_connect_timeout=g_tcp_connect_timeout->getValue();

        g_tcp_connect_timeout->addListener([](const int& old_value,const int& new_value){
            FIBER_LOG_INFO(g_logger)<<"tcp connect timeout changed from "<<old_value<<"to "
            <<new_value;
            s_connect_timeout=new_value;
    });
    }
};
static _HookIniter s_hook_initer;
bool is_hook_enable(){
    return t_hook_enable;
}
void set_hook_enable(bool flag){
    t_hook_enable=flag;
}
}
struct timer_info{
    int cancelled=0;
};
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd,OriginFun fun,const char* hook_fun_name
    ,uint32_t event,int timeout_so,Args&&... args){
        if(!FiberServer::t_hook_enable){
            return fun(fd,std::forward<Args>(args)...);
        }
        FiberServer::FdCtx::ptr ctx=FiberServer::FdMgr::GetInstance()->get(fd);
        if(!ctx){//如果该fd没被FDMgr管理的话(比如m_tickleFds[1]) 则直接进行io
            return fun(fd,std::forward<Args>(args)...);
        }
        
        if( ctx->isClose()){
            errno = EBADF;
            // FIBER_LOG_INFO(g_logger)<<1;
            return -1;
        }
        if(!ctx->isSocket()||ctx->getUserNonblock()){
            FIBER_LOG_INFO(g_logger)<<1;
            return fun(fd,std::forward<Args>(args)...);
        }
        uint64_t to = ctx->getTimeout(timeout_so);
        std::shared_ptr<timer_info> tinfo(new timer_info); 

retry:
        ssize_t n =fun(fd,std::forward<Args>(args)...);
        while(n==-1 && errno==EINTR){
             n = fun(fd, std::forward<Args>(args)...);
        }
        if(n==-1 &&errno ==EAGAIN){
            FiberServer::IOManager* iom=FiberServer::IOManager::GetThis();
            FiberServer::Timer::ptr timer;
            std::weak_ptr<timer_info> winfo(tinfo);

            if(to != (uint64_t)-1) {
                timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                    auto t = winfo.lock();
                    if(!t || t->cancelled) {
                        return;
                    }
                    t->cancelled = ETIMEDOUT;
                    iom->cancelEvent(fd, (FiberServer::IOManager::Event)(event));
                }, winfo);
            }
            int rt=iom->addEvent(fd,(FiberServer::IOManager::Event)(event));
            if(FIBER_UNLIKELY(rt)){
                   FIBER_LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
                << fd << ", " << event << ")";
            if(timer) {
                timer->cancel();
            }
            return -1;
            }
            else{
                FiberServer::Fiber::YieldToHold();
                if(timer){
                    timer->cancel();
                }
                if(tinfo->cancelled){
                    errno = tinfo->cancelled;
                    return -1;
                }
                goto retry;
            }
        }
        return n;
}
extern "C"{
#define XX(name) name ## _fun name ##_f = nullptr;
    HOOK_FUN(XX);
#undef XX
unsigned int sleep(unsigned int seconds){
    if(!FiberServer::t_hook_enable){
        return sleep_f(seconds);
    }
    FiberServer::Fiber::ptr fiber =  FiberServer::Fiber::GetThis();
    FiberServer::IOManager *iom = FiberServer::IOManager::GetThis();
    iom->addTimer(seconds*1000,[iom,fiber](){
        iom->schedule(fiber,-1);
    });
    FiberServer::Fiber::YieldToHold();
    return 0;
}
int usleep(useconds_t usec){
    if(!FiberServer::t_hook_enable){
        return usleep_f(usec);
    }
    FiberServer::Fiber::ptr fiber =  FiberServer::Fiber::GetThis();
    FiberServer::IOManager *iom = FiberServer::IOManager::GetThis();
    iom->addTimer(usec/1000,[iom,fiber](){
        iom->schedule(fiber,-1);
    });
    FiberServer::Fiber::YieldToHold();
    return 0;
}
int nanosleep(const struct timespec *req, struct timespec *rem){
    if(!FiberServer::t_hook_enable){
        return nanosleep_f(req,rem);
    }
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    FiberServer::Fiber::ptr fiber =  FiberServer::Fiber::GetThis();
    FiberServer::IOManager *iom = FiberServer::IOManager::GetThis();
    iom->addTimer(timeout_ms,[iom,fiber](){
        iom->schedule(fiber,-1);
    });
    FiberServer::Fiber::YieldToHold();
    return 0;
}
int socket(int domain,int type ,int protocol){
    if(!FiberServer::t_hook_enable){
        return socket_f(domain,type,protocol);
    }
    int fd=socket_f(domain,type,protocol);
    if(fd==-1){
        return fd;
    }
    FiberServer::FdMgr::GetInstance()->get(fd, true);
    return fd;
}
int connect_with_timeout(int fd,const struct sockaddr *addr,socklen_t addrlen,uint64_t timeout_ms){
    if(!FiberServer::t_hook_enable){
        return connect_f(fd,addr,addrlen);
    }
    FiberServer::FdCtx::ptr ctx = FiberServer::FdMgr::GetInstance()->get(fd);
    if(!ctx||ctx->isClose()){
        errno = EBADF;
        return -1;
    }
    if(!ctx->isSocket()) {
        return connect_f(fd, addr, addrlen);
    }

    if(ctx->getUserNonblock()) {
        return connect_f(fd, addr, addrlen);
    }
    
    int n= connect_f(fd,addr,addrlen);
    if(n==0){
        return 0;
    }
    else if(n!=-1 || errno != EINPROGRESS){
        return n;
    }
    FiberServer::IOManager *iom = FiberServer::IOManager::GetThis();
    FiberServer::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);
    if(timeout_ms!=(uint64_t)-1){
        timer = iom->addConditionTimer(timeout_ms,[winfo,fd,iom](){
            auto t =winfo.lock();
            if(!t||t->cancelled){
                return ;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd,FiberServer::IOManager::WRITE);
        },winfo);
    }
    int rt= iom->addEvent(fd,FiberServer::IOManager::WRITE);
    if(rt == 0){
        FiberServer::Fiber::YieldToHold();
        if(timer){
            timer->cancel();
        }
        if(tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    }
    else{
        if(timer){
            timer->cancel();
        }
        FIBER_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }
    int error=0;
    socklen_t len = sizeof(int);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)){
        return -1;
    }
    if(!error){
        return 0;
    }
    else{
        errno = error;
        return -1;
    }
}
int connect(int sockfd,const struct sockaddr* addr ,socklen_t addrlen){
    return connect_with_timeout(sockfd,addr,addrlen,FiberServer::s_connect_timeout);
}
int accept(int s,struct sockaddr *addr,socklen_t *addrlen){
    int fd = do_io(s,accept_f,"accept",FiberServer::IOManager::READ,SO_RCVTIMEO,addr,addrlen);
    if(fd >0){
        FiberServer::FdMgr::GetInstance()->get(fd,true);
    }
    return fd;
}
ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", FiberServer::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", FiberServer::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", FiberServer::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", FiberServer::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd,struct msghdr *msg,int flags){
    return do_io(sockfd, recvmsg_f, "recvmsg", FiberServer::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", FiberServer::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", FiberServer::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", FiberServer::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", FiberServer::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", FiberServer::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}
int close(int fd){
    if(!FiberServer::t_hook_enable){
        return close_f(fd);
    }
    FiberServer::FdCtx::ptr ctx=FiberServer::FdMgr::GetInstance()->get(fd);
    if(ctx){
        auto iom = FiberServer::IOManager::GetThis();
        if(iom){
            iom->cancelAll(fd);
        }
        FiberServer::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

int fcntl(int fd,int cmd,...){
    va_list va;
    va_start(va,cmd);
    switch(cmd){
        case F_SETFL:{
            int arg=va_arg(va,int);
            va_end(va);
            FiberServer::FdCtx::ptr ctx = FiberServer::FdMgr::GetInstance()->get(fd);
            if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    return fcntl_f(fd, cmd, arg);
            }
            ctx->setUserNonblock(arg & O_NONBLOCK);
            if(ctx->getSysNonblock()){
                arg |=O_NONBLOCK;
            }
            else{
                arg &=~O_NONBLOCK;
            }
            return fcntl_f(fd,cmd,arg);
        }
        break;
        case F_GETFL:{
            va_end(va);
            int arg=fcntl_f(fd,cmd);
            FiberServer::FdCtx::ptr ctx=FiberServer::FdMgr::GetInstance()->get(fd);
              if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    return arg;
                }
                if(ctx->getUserNonblock()) {
                    return arg | O_NONBLOCK;
                } else {
                    return arg & ~O_NONBLOCK;
                }
        }
        break;
         case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
              case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
    }
int ioctl(int d,unsigned long int request,...){
    va_list va;
    va_start(va,request);
    void* arg=va_arg(va,void*);
    va_end(va);
    if(request==FIONBIO){
        bool user_nonblock= !!*(int*)arg;
        FiberServer::FdCtx::ptr ctx = FiberServer::FdMgr::GetInstance()->get(d);
        if(!ctx||ctx->isClose()||!ctx->isSocket()){
            return ioctl_f(d,request,arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d,request,arg);
}
int getsockopt(int sockfd, int level, int optname,void *optval, socklen_t* optlen) {
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if(!FiberServer::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET) {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            FiberServer::FdCtx::ptr ctx = FiberServer::FdMgr::GetInstance()->get(sockfd);
            if(ctx) {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
