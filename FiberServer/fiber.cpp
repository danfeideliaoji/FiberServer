#include "fiber.h"
#include<atomic>
#include "base/log.h" 
#include "base/config.h"
#include "base/macro.h"
#include "scheduler.h"
namespace FiberServer{
    static Logger::ptr g_logger=FIBER_LOG_NAME("system");//指定日志器 底层都为system
    static ConfigVar<uint32_t>::ptr g_fiber_stack_size=Config::Lookup<uint32_t>("fiber.stack_size"
        ,128*1024,"fiber stack size");//注册协程栈大小配置项 有默认值128kB
    
    
    static thread_local Fiber* t_fiber=nullptr; //当前正在运行的协程
    
    static thread_local Fiber::ptr t_threadFiber=nullptr; //用协程线程上下文(该协程没有独立栈空间 回调函数)
    //如果是主线程的话则表示主线程的上下文便于主协程调用完回去
    //子线程的话 跟主协程一样表示run的上下文 和t_scheduler_fiber是一样的
    
    //atomic重载了++ -- 这些是线程安全的
    static std::atomic<uint64_t> s_fiber_id{0};//协程di 因为多线程 所以用原子变量
    static std::atomic<uint64_t> s_fiber_count{0};//协程总数
    class MallocStackAllocator{//内存分配器
    public:
        static void* Alloc(size_t size){
            return malloc(size);
        }
        static void Dealloc(void* ptr,size_t size){
            //这里是为了接口统一 所有要传size
            free(ptr);
        }
    };
    using StackAllocator =MallocStackAllocator;//指定栈内存分配器 
    //后续可以指定其他类型的

    uint64_t Fiber::GetFiberId(){//获得当前运行的协程id
        if(t_fiber){
            return t_fiber->getId();
        }
        return 0;//如果线程当前没有协程运行 返回0
    }
    Fiber::Fiber(){//无参构造 是GetThis 作为当前线程的主协程
        m_state=State::EXEC;
        SetThis(this);//设置当前运行协程
        if(getcontext(&m_ctx)){
            FIBER_ASSERT2(false,"getcontext error");
        }
        ++s_fiber_count;
        FIBER_LOG_DEBUG(g_logger)<<"Thread name= "<<Thread::GetName() <<" Fiber::Fiber main "<<"id= "<<m_id;
    }
    //任务协程
    Fiber::Fiber(std::function<void()>cb ,size_t stacksize,bool use_caller)//普通协程构造函数
    :m_id(++s_fiber_id),
    m_cb(cb) {
        ++s_fiber_count;
        m_stacksize=stacksize ?stacksize :g_fiber_stack_size->getValue();//指定协程栈大小 如果指定0 则从配置获取
        // FIBER_LOG_DEBUG(g_logger)<<"Fiber_stacksize= "<<m_stacksize;
        m_stack = StackAllocator::Alloc(m_stacksize);//给栈分配内存
        if(getcontext(&m_ctx)){
            FIBER_ASSERT2(false,"getcontext error");
        }
        m_ctx.uc_link=nullptr;//指定后继上下文为空 
        m_ctx.uc_stack.ss_sp = m_stack ; //指定栈指针
        m_ctx.uc_stack.ss_size = m_stacksize;//指定栈大小
        if(!use_caller){
            makecontext(&m_ctx,&Fiber::MainFunc,0);//指定协程运行函数
        }
        else{
            makecontext(&m_ctx,&Fiber::CallerMainFunc,0);//指定协程运行函数
        }
        FIBER_LOG_DEBUG(g_logger)<<"Fiber::Fiber id="<<m_id;
    }
    Fiber::~Fiber(){
        --s_fiber_count;
        if(m_stack){//这里用是否有栈空间区分是否为主协程(主协程没有独立的栈空间)
            FIBER_ASSERT(m_state==State::INIT||
                m_state==State::EXCEPT||
                m_state==State::TERM
            );//删除协程时 必须是初始化 异常 可执行这三种状态
            StackAllocator::Dealloc(m_stack,m_stacksize);
        }
        else{//为主协程
            FIBER_ASSERT(!m_cb);//主协程不允许有回调函数
            FIBER_ASSERT(m_state==State::EXEC);
            Fiber* cur=t_fiber;
            if(cur==this){
                SetThis(nullptr);
            }
        }
        FIBER_LOG_DEBUG(g_logger)<<"Fiber::~Fiber id="<<
        m_id<<" total="<<s_fiber_count;
    }
    void Fiber::reset(std::function<void()> cb){//重置协程函数 当前协程处于TERM/EXCEPT状态时可以重置协程函数并恢复执行
        FIBER_ASSERT(m_stack) ;// 协程栈不能为空
        FIBER_ASSERT(m_state==State::TERM||
        m_state==State::EXCEPT||
        m_state==State::INIT
        );
        m_cb=cb;
        if(getcontext(&m_ctx)){
            FIBER_ASSERT2(false,"getcontext error");
        }
        m_ctx.uc_link=nullptr;//指定后继上下文为空 
        m_ctx.uc_stack.ss_sp = m_stack ; //指定栈指针
        m_ctx.uc_stack.ss_size = m_stacksize;//指定栈大小
        makecontext(&m_ctx,&Fiber::MainFunc,0);
        m_state=State::INIT;//重置后状态为初始化状态
    }
    
    //call back是针对主线程的 如果主线程也要执行任务(stop)时
    //由call back从主线程切换到主协程 住协程回到主线程
    void Fiber::call(){//主线程调用 由主线程的调度器调用（stop里）
        SetThis(this);//先设置当前运行的协程
        m_state=State::EXEC;
        if(swapcontext(&t_threadFiber->m_ctx,&m_ctx)){
            FIBER_ASSERT2(false,"swapcontext error");
        }
    }
    void Fiber::back(){
        SetThis(t_threadFiber.get());
        if(swapcontext(&m_ctx,&t_threadFiber->m_ctx)){
            FIBER_ASSERT2(false,"swapcontext error");
        }
    }
    void Fiber::swapIn(){//将当前协程切换为运行状态
        //从主调度器切换到任务协程
        SetThis(this);
        FIBER_ASSERT(m_state!=State::EXEC);
        m_state=State::EXEC;
        if(swapcontext(&Scheduler::GetMainFiber()->m_ctx,&m_ctx)){
            FIBER_ASSERT2(false,"swapcontext");
        }
    }
    void Fiber::swapOut(){//当前协程切换到后台
        SetThis(Scheduler::GetMainFiber());
        if(swapcontext(&m_ctx,&Scheduler::GetMainFiber()->m_ctx)){
            FIBER_ASSERT2(false,"swapcontext");
        }
    }
    void Fiber::SetThis(Fiber *f){//设置当前线程的运行协程
        t_fiber=f;
    }
    Fiber::ptr Fiber::GetThis(){//获得当前线程的运行协程如果没有则创建主协程作为调度使用
        if(t_fiber){
            return t_fiber->shared_from_this();
        }
        //如果当前还没有协程(fiber创建后t_fiber是时刻有值的)
        Fiber::ptr main_fiber(new Fiber);//用无参构造创建主协程
        FIBER_ASSERT(t_fiber==main_fiber.get());// 看是否创建失败 
        t_threadFiber=main_fiber;//设置线程主协程
        return t_fiber->shared_from_this();
    }
    void Fiber::YieldToReady(){////协程切换到后台，并且设置为Ready状态
        Fiber::ptr cur=GetThis();
        FIBER_ASSERT(cur->m_state==State::EXEC);
        cur->m_state=State::READY;
        cur->swapOut();
    }
    void Fiber::YieldToHold(){////协程切换到后台，并且设置为Hold状态
        Fiber::ptr cur=GetThis();
        FIBER_ASSERT(cur->m_state==State::EXEC);
        //不在swapOut前设置HOLD 保持EXEC状态 防止其他线程在swapOut完成前
        //通过triggerEvent重新调度该协程导致竞态条件
        cur->swapOut();
    }
    uint64_t Fiber::TotalFibers(){//返回协程总数
        return s_fiber_count;
    }
    void Fiber::MainFunc(){//协程执行函数 执行完返回主协程
        Fiber::ptr cur=GetThis();//返回的是共享指针
        FIBER_ASSERT(cur);
        try{
            cur->m_cb();//执行协程函数
            cur->m_cb=nullptr;//执行完释放
            cur->m_state=State::TERM;//设置状态为结束态
        }catch(std::exception& e){
            cur->m_state=State::EXCEPT;//设置异常状态
            FIBER_LOG_ERROR(g_logger)<<"Fiber Except: "
            <<e.what()
            <<" fiber id="<<cur->getId()
            <<std::endl
            <<FiberServer::BacktraceToString();
        }
        catch(...){
            cur->m_state=State::EXCEPT;//设置异常状态
            FIBER_LOG_ERROR(g_logger)<<"Fiber Except"
            <<" fiber id="<<cur->getId()<<std::endl
            <<FiberServer::BacktraceToString();
        }
        //这里非常妙 如果直接用cur切走的话 cur这个共享指针一直 不会消失
        //这样就会导致共享协程指针计数一直不为0 协程对象就不会被销毁 导致你内存泄露
        //所以最后一定要将cur这个共享指针释放掉
        auto raw_ptr=cur.get();
        cur.reset();
        raw_ptr->swapOut();
        
        FIBER_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
    }
    void Fiber::CallerMainFunc(){//协程执行函数 封装主线程的run
        Fiber::ptr cur=GetThis();
        FIBER_ASSERT(cur);
        try{
            cur->m_cb();//执行协程函数 后面的都是收尾
            cur->m_cb=nullptr;
            cur->m_state=State::TERM;//完成
        }catch(std::exception& e){
            cur->m_state=State::EXCEPT;//异常
            FIBER_LOG_ERROR(g_logger)<<"Fiber Except: "
            <<e.what()<<
            " fiber id= "<<cur->getId()
            <<std::endl<<FiberServer::BacktraceToString();
        }
        catch(...){
            cur->m_state=State::EXCEPT;
            FIBER_LOG_ERROR(g_logger)<<"Fiber Except "
            <<" fiber id="<<cur->getId()
            <<std::endl<<FiberServer::BacktraceToString();
        }
        auto raw_ptr=cur.get();
        cur.reset();
        raw_ptr->back();//backd当主线程run执行完回到主线程上下文
        FIBER_ASSERT2(false,"never reach fiber_id=" 
            + std::to_string(raw_ptr->getId()))
    }
    //重载输出fiber状态
 std::ostream& operator<<(std::ostream& os,Fiber::State state){
        switch(state){
#define XX(s)\
        case Fiber::State::s:\
            return os<<#s;
        XX(INIT);
        XX(HOLD);
        XX(EXEC);
        XX(TERM);
        XX(READY);
        XX(EXCEPT);
#undef XX 
        default:
            FIBER_ASSERT2(false,"Fiber state return error");
        }
    }
}
