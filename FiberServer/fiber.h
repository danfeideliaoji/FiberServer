#pragma once
#include<boost/context/fiber.hpp>
#include<memory>
#include<iostream>
#include<functional>
#include"base/macro.h"
namespace FiberServer{
    class Scheduler;
    class Fiber:public std::enable_shared_from_this<Fiber> {
    public:
        friend class Scheduler;
        typedef std::shared_ptr<Fiber> ptr;
        enum class State{
            INIT,// 初始化态
            HOLD,//暂停状态
            EXEC,// 执行状态
            TERM,//结束状态
            READY,//就绪状态
            EXCEPT//异常状态
        };
        
        private:
        Fiber();//主协程构造函数
        public:
        Fiber(std::function<void()> cb,size_t stacksize=0, bool use_caller=false);//普通协程构造函数
        ~Fiber();
        void reset(std::function<void()> cb);//重置协程函数 当前协程处于TERM/EXCEPT状态时可以重置协程函数并恢复执行
        void swapIn(); //将当前协程切换为运行状态
        void swapOut();//当当前协程切换到后台
        void call(); //将当前线程切换为执行状态
        void back(); //将当前线程切换到后台
        uint64_t getId() const{return m_id;}
        State getState() const{return m_state;}
        public:
        
        static void SetThis(Fiber *f);//设置当前线程的运行协程
        
        static Fiber::ptr GetThis();//获得当前线程的运行协程如果没有则创建主协程作为调度使用
        
        static void YieldToReady(); //将当前协程切换到后台,并设置为READY状态
        
        static void YieldToHold(); //将当前协程切换到后台,并设置为HOLD状态
        
        static uint64_t TotalFibers(); //返回协程总数
        //下面两个函数一定要用静态 非静态会有一个隐形参数Fiber* this
        static void MainFunc();//协程执行函数 执行完后返回主协程
        static void CallerMainFunc();//协程执行函数 执行完后返回线程调度协程
        static uint64_t GetFiberId(); //获取当前协程id
    private:
        uint64_t m_id=0; //协程id
        boost::context::fiber m_ctx; //协程上下文
        boost::context::fiber m_caller; //调用方上下文
        State m_state=State::INIT ; //协程状态
        uint32_t m_stacksize=0;//协程栈大小
        bool m_useCaller=false;
        std::function<void()> m_cb;//协程运行函数
        void makeContext();
    };
    //重载输出fiber状态
 std::ostream& operator<<(std::ostream& os,Fiber::State state);
}
