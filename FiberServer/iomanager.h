#pragma once
#include "scheduler.h"
#include "time.h"
#include "base/mutex.h"
namespace FiberServer{
//IOManager是用来管理io阻塞的 当有消息时 再加到调度器里
//shcheduler 是用来管理调度的 
//通过继承实现模块解耦 分层
class IOManager: public Scheduler,public TimerManager{
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;
    enum Event{
        NONE=0x0,
        READ=0x1,
        WRITE=0x4
    };
private:
    struct FdContext{
        typedef Mutex MutexType;
        struct EventContext{
            Scheduler* scheduler=nullptr;//事件执行的调度器 裸指针 这里这是调用
            Fiber::ptr fiber;// 事件协程
            std::function<void()> cb;// 事件的回调函数
        };
        
        EventContext& getContext(Event event);//获取指定事件上下文类
        
        void resetContext(EventContext& ctx);// 重置事件上下文(读事件 写事件)
        
        void triggerEvent(Event event);//触发指定的事件
        
        EventContext read; /// 读事件上下文
        
        EventContext write; /// 写事件上下文
        
        int fd=0; /// 事件关联的句柄
        
        Event events=NONE;/// 当前拥有的所有事件 用位掩码表示
        
        MutexType mutex;/// 事件的Mutex
    };
public:
    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否将调用线程包含进去
     * @param[in] name 调度器的名称
     */
    IOManager(size_t threads=1,bool use_caller=true,const std::string& name="");
    
    ~IOManager();
    
    /**
     * @brief 添加事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @param[in] cb 事件回调函数
     * @return 添加成功返回0,失败返回-1
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

     /**
     * @brief 删除事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 不会触发事件
     */
    bool delEvent(int fd, Event event);

    bool cancelEvent(int fd,Event event);//完成事件(调用其triggerEvent) 并取消对应事件

    /**
     * @brief 执行并取消fd所有事件
     * @param[in] fd socket句柄
     */
    bool cancelAll(int fd);

    static IOManager* GetThis();//返回当前的IOManager
protected:
    void tickle() override; //通知有事件了
    bool stopping() override;//是否可以停止
    void idle() override; //没有事件执行idle
    void onTimerInsertedAtFront() override;//当有新的定时器插入到定时器的首部,执行该函数
    
    void contextResize(size_t size);//设置context的大小

    bool stopping(uint64_t& timeout);//是否可以停止

private:
    int m_epfd = 0; //epoll 事件句柄
    int m_tickleFds[2]; //tickle用的管道 0读 1写
    std::atomic<size_t> m_pendingEventCount{0};//待执行的事件数量
    RWMutexType m_mutex;
    std::vector<FdContext*> m_fdContexts; //socket事件上下文容器
};
}