#include "iomanager.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include "base/macro.h"
#include "base/log.h"
#include "fiber.h"
namespace FiberServer{
static Logger::ptr g_logger=FIBER_LOG_NAME("system");

enum EpollCtlOp {
};

//在日志输出这add mod del三个epoll操作名  cpp没有反射无法直接获得变量名 这里通过宏实现
static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl) \
        case ctl: \
            return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default:
            return os << (int)op;
    }
#undef XX
}

static std::ostream& operator<< (std::ostream& os,EPOLL_EVENTS events){
    if(!events){
        return os<<"0";
    }
    bool first =true;
#define XX(E)\
    if(events&E){\
        if(!first){\
            os<<"|";\
        }\
        os<<#E;\
        first=false;\
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}
IOManager::FdContext::EventContext& IOManager::FdContext::getContext(Event event){
    switch(event){
        case READ:
            return read;
        case WRITE:
            return write;
        default:
            FIBER_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}
void IOManager::FdContext::resetContext(IOManager::FdContext::EventContext& ctx){//重置事件上下文(读事件 写事件)
    ctx.scheduler=nullptr;
    ctx.fiber=nullptr;
    ctx.cb=nullptr;
} 
void IOManager::FdContext::triggerEvent(IOManager::Event event){//触发指定的事件
    FIBER_ASSERT(events & event);//要触发的事件必须存在
    events = (Event)(events & ~event);//删除events里的evnet事件
    EventContext& ctx = getContext(event);
    //把对应的事件交给调度器
    //注意这里都把地址交给调度器 调度器进行swap一方面是所有权的转让 另一方面
    //是性能问题 swap只用交换一下就行
    if(ctx.cb){
        ctx.scheduler->schedule(&ctx.cb);
    }else if(ctx.fiber){
        ctx.scheduler->schedule(&ctx.fiber);
    }
    //cb fiber都为空了 所有不用管了

    ctx.scheduler=nullptr;
    return;
}
IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    :Scheduler(threads, use_caller, name) //执行父类的构造函数
    {
    m_epfd = epoll_create(5000);//创建epoll 5000参数无实际意义
    FIBER_ASSERT(m_epfd > 0);

    int rt = pipe(m_tickleFds);//创建管道 tickle时使用 epoll负责读 tickle写
    FIBER_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    //epoll的所有事件都用边缘触发 只有事件到来时 epoll才会触发
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];//监听读的那一端

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);//设置非阻塞
    FIBER_ASSERT(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    FIBER_ASSERT(!rt);

    contextResize(32);

    start();//启动多线程
}
IOManager::~IOManager() {
    stop();//优雅 先停止多线程
    //手动关闭创建的句柄
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}
void IOManager::contextResize(size_t size) {//设置context的大小
    m_fdContexts.resize(size);
    for(size_t i=0;i<m_fdContexts.size();++i){
        if(!m_fdContexts[i]){
            m_fdContexts[i]=new FdContext;
            m_fdContexts[i]->fd=i;
        }
    }
}
int IOManager::addEvent(int fd,Event event,std::function<void()>cb){
    FdContext* fd_ctx =nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size()>fd){
        fd_ctx=m_fdContexts[fd];
        lock.unlock();
    }
    else{
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd*1.5);
        fd_ctx = m_fdContexts[fd];
    }
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(FIBER_UNLIKELY(fd_ctx->events & event)){
        FIBER_LOG_ERROR(g_logger)<<"addEvent assert fd= "<<fd
        <<"event = "<<(EPOLL_EVENTS)event<<
        "fd_ctx.event= "<<(EPOLL_EVENTS)fd_ctx->events;
    }
    int op =fd_ctx->events ? EPOLL_CTL_MOD:EPOLL_CTL_ADD;//原来没注册 add有mod修改
    epoll_event epevent;
    epevent.events=EPOLLET|fd_ctx->events|event ;//边缘触发一直有
    epevent.data.ptr =fd_ctx;
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt) {
        FIBER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
            << (EPOLL_EVENTS)fd_ctx->events;//学学大佬的日志输出
        return -1;
    }
    ++m_pendingEventCount; //待处理事件+1
    fd_ctx->events = (Event)(fd_ctx->events | event);//events加上对应的任务 必须强转 因为枚举或运算后为int了
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    FIBER_ASSERT(!event_ctx.scheduler
                && !event_ctx.fiber
                && !event_ctx.cb);//fd的上下文必须为空 后面会设置
    event_ctx.scheduler = Scheduler::GetThis();//获得当前线程的调度器     
    if(cb) {
        event_ctx.cb.swap(cb);
    } else {
        event_ctx.fiber = Fiber::GetThis();
        FIBER_ASSERT2(event_ctx.fiber->getState() == Fiber::State::EXEC
                      ,"state: " <<event_ctx.fiber->getState());//第二个参数看着很奇怪 其实这里利用了宏
                      //宏展开 第二个参数是log的日志输出 相当于<<"state"<< event_ctx.fiber->getState()
    }
    return 0;       
}
bool IOManager::delEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();
    //很细节 对events操作用整体的读写锁 对里面的单个操作的话用其自己的锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(FIBER_UNLIKELY(!(fd_ctx->events & event))) {//不大可能没有要删除的事件
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);//;利用位运算删除事件 
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;//evetns没事件的话就从epoll删除了
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        FIBER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --m_pendingEventCount;//减少待执行的事件
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);//重置删除的事件
    return true;
}
bool IOManager::cancelEvent(int fd,Event event){
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(FIBER_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        FIBER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    fd_ctx->triggerEvent(event);//触发对应的事件 相应的事件清楚也在其中
    --m_pendingEventCount;
    return true;
}
bool IOManager::cancelAll(int fd){//触发并取消fd所有事件
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events) {
        return false;
    }
    int op=EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        FIBER_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    if(fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if(fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    FIBER_ASSERT(fd_ctx->events == 0);//事件都取消了 所以events要为0

    return true;
}
IOManager* IOManager::GetThis() {
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}
void IOManager::tickle() {//通知有事件了
    if(!hasIdleThreads()) {
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);//0读 1写
    FIBER_ASSERT(rt == 1);
}
bool IOManager::stopping(uint64_t& timeout) {//是否可以停止
    timeout = getNextTimer();
    return timeout == ~0ull //定时器没有任务了
        && m_pendingEventCount == 0
        && Scheduler::stopping();//父类管理的调度
//IOManager是用来管理io阻塞的 当有消息时 再加到调度器里
//shcheduler 是用来管理调度的 
//通过继承实现模块解耦 分层
}
bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

void IOManager::idle(){
    FIBER_LOG_DEBUG(g_logger)<<"idle";
    const uint64_t MAX_EVENTS=256;
    epoll_event* events=new epoll_event[MAX_EVENTS];//创建多个epoll_event
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
        delete[] ptr;
    });//自定义删除器 析构时删除所有
    while(true){
        uint64_t next_timeout=0;
        if(FIBER_UNLIKELY(stopping(next_timeout))){
              FIBER_LOG_INFO(g_logger) << "name=" << getName()
                                     << " idle stopping exit";
            break;
        }
        // FIBER_LOG_DEBUG(g_logger)<<next_timeout;
        int rt=0;
        do{
            static const int MAX_TIMEOUT = 3000;//细节静态 避免重复创建 单位都是毫秒
            
            //保证next_timeout线程sleep时间不超过MAX_TIEMOUT
            if(next_timeout!=~0ull){
                next_timeout=(int)next_timeout>MAX_TIMEOUT ? MAX_TIMEOUT : next_timeout;
            }else{
                next_timeout=MAX_TIMEOUT;
            }
            rt=epoll_wait(m_epfd,events,MAX_EVENTS,(int)next_timeout);//指定存放的event和最长等待时间
            if(rt<0&&errno==EINTR){

            }
            else{
                break;
            }
        }while(true);

        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);//列出该执行的定时器任务
        if(!cbs.empty()){
            schedule(cbs.begin(),cbs.end());//把任务交给调度器
            cbs.clear();
        }

        for(int i=0; i< rt;++i ){//rt为int 这里也用int索引
            epoll_event& event=events[i];
            if(event.data.fd == m_tickleFds[0]){
                uint8_t dummy[256];
                while(read(m_tickleFds[0],dummy,sizeof(dummy)) > 0){
                  
                }
                continue;
            }
            FdContext* fd_ctx =(FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            if(event.events &(EPOLLERR|EPOLLHUP)){
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;//过滤到读写事件外的
            }
            int real_events = NONE;//将epoll event的事件转为fd的自己event(等价的)读写
            if(event.events & EPOLLIN){
                real_events |= READ;
            }
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }
            if((fd_ctx->events & real_events) == NONE) {//如果没有要关注的
                continue;
            }
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;// 边缘触发不能丢
            
            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if(rt2){
                FIBER_LOG_ERROR(g_logger)<< "epoll_ctl(" << m_epfd << ", "
                    << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }
            if(real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if(real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        
        }
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->swapOut();
    }
}
void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}
