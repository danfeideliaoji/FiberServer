#pragma once
#include<memory>
#include<functional>
#include<vector>
#include<set>
#include"thread.h"
namespace FiberServer{
class TimerManager;
class Timer:public std::enable_shared_from_this<Timer>//定时器任务
{
    public:
        friend class TimerManager;
        typedef std::shared_ptr<Timer> ptr;
        bool cancel();//取消定时器
        bool refresh();//刷新定时器
        bool reset(uint64_t ms,bool from_now);//重置定时器 from_now表示是否从当前时间开始计算
    private:
    //构造函数私有 只能通过TimerManager创建
        Timer(uint64_t ms,std::function<void()> cb,bool recurring,TimerManager* managger);
        Timer(uint64_t next);
    private:
        bool m_recurring= false;//是否循环定时
        uint64_t m_ms=0;//定时间隔
        uint64_t m_next=0;//执行时间
        std::function<void()> m_cb;//回调函数

        TimerManager* m_manager=nullptr;//定时器管理类 
        //不能用智能指针 否则会循环引用 

    public:
        struct Comparator{//比较定时器的智能指针的大小(按执行时间排序)
            bool operator()(const Timer::ptr& lhs,const Timer::ptr& rhs)const;
        };
};
class TimerManager{//定时器管理类
    friend class Timer;
    public:
        typedef RWMutex RWMutexType;
        TimerManager();
        virtual ~TimerManager();
        Timer::ptr addTimer(uint64_t ms,std::function<void()>cb,bool 
        recurring=false);//添加定时器  recurring 是否循环定时器
        
        Timer::ptr addConditionTimer(uint64_t ms,std::function<void()> cb,
        std::weak_ptr<void>weak_cond ,bool recurring=false);//添加条件定时器
        //weak_cond 条件

        uint64_t getNextTimer();//到最近一个定时器执行的时间间隔(毫秒)
       
        void listExpiredCb(std::vector<std::function<void()>>&cbs);//获取需要执行的定时器的回调函数列表

        bool  hasTimer();// 是否有定时器
protected:
        virtual void onTimerInsertedAtFront()=0;//当有新的定时器插入到定时器的首部,执行该函数 IOManager重写在里面进行了tickle
        
        void addTimer(Timer::ptr val,RWMutexType::WriteLock& lock);// 将定时器添加到管理器中
private:
        bool detectClockRollover(uint64_t now);// 检测服务器时间是否被调后了

private:
        RWMutexType m_mutex;
        std::set<Timer::ptr,Timer::Comparator> m_timers;//用红黑树管理定时器
        //不用堆的原因是堆不支持访问任一节点 删除和刷新定时器不方便
        
        bool m_tickled = false;
        uint64_t m_previouseTime = 0; //上次获得的时间
};  
}

