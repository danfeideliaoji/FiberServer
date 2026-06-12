#pragma once
#include <memory>
#include <vector>
#include <list>
#include <string>
#include <iostream>
#include <functional>
#include <atomic>
#include <utility>
#include "fiber.h"
#include "thread.h"
#include "base/mutex.h"
namespace FiberServer{
class Scheduler{
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;
    Scheduler(size_t threads=1,bool use_caller=true,
    const std::string& name="");//use_caller代表是否使用当前调用线程
    
    virtual ~Scheduler();//因为可能要继承 所有用虚函数

    const std::string& getName() const {return m_name;}
    static Scheduler* GetThis();//返回当前协程调度器
    static Fiber* GetMainFiber();//返回当前协程调度器的调度协程
    // 每个 P 的运行时统计，用于 /api/status、单元测试和压测分析。
    struct ProcessorStats {
        int id = -1;
        // 当前 P 本地队列里等待执行的任务数量。
        size_t queue_size = 0;
        // 直接投递到当前 P 本地队列的任务数量。
        size_t schedule_count = 0;
        // 绑定到当前 P 的工作线程累计执行的任务数量。
        size_t execute_count = 0;
        // 按任务来源拆分执行次数，这三项相加应等于 execute_count。
        // 压测时可以通过它判断任务主要来自本地队列、全局队列还是 steal。
        size_t local_execute_count = 0;
        size_t global_execute_count = 0;
        size_t steal_execute_count = 0;
        // 当前 P 从全局队列搬运的任务数量。
        // global_batch_count 是搬运批次数，global_pull_count 是任务总数。
        size_t global_pull_count = 0;
        size_t global_batch_count = 0;
        // 当前 P 从其他 P 队列偷到的任务数量。
        // steal_batch_count 是成功偷取批次数，steal_count 是任务总数。
        size_t steal_count = 0;
        size_t steal_batch_count = 0;
        // steal 尝试和失败次数，用来观察是否存在大量空转偷取。
        size_t steal_attempt_count = 0;
        size_t steal_fail_count = 0;
    };
    // 调度器状态快照，仅用于观测和调试。
    // 外部代码不要把它当作无锁同步依据。
    struct SchedulerStats {
        std::string name;
        size_t global_queue_size = 0;
        size_t global_schedule_count = 0;
        size_t active_thread_count = 0;
        size_t idle_thread_count = 0;
        std::vector<ProcessorStats> processors;
    };
    SchedulerStats getStats();
    void start();//启动协程调度器
    void stop();//停止协程调度器 
    template<typename FiberOrCb>
    void schedule(FiberOrCb fc,int thread=-1){//加任务
        bool need_tickle =false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc,thread);
        }
        if(need_tickle){
            tickle();
        }
    }
    template<class InputIterator>//用迭代器加多个任务
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            while(begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                ++begin;
            }
        }
        if(need_tickle) {
            tickle();
        }
    }
    void switchTo(int thread = -1);
    std::ostream& dump(std::ostream& os);
protected:

    virtual void tickle();//通知协程调度器有任务了

    void run();//协程调度函数
    virtual bool stopping();//返回是否可以停止
    virtual void idle();//协程调度器空闲时执行的函数
    void setThis();//设置当前协程调度器
    bool hasIdleThreads(){return m_idleThreadCount >0 ;}//是否有空闲线程  用来判断是否要tickle
private:
    struct FiberAndThread{ //协程/函数/线程组
        Fiber::ptr fiber;//协程
        std::function<void()> cb;//协程执行函数
        int thread;//线程id
        // 四种有参构造函数 应对各种情况
        FiberAndThread(Fiber::ptr f,int thr):
        fiber(f),thread(thr){
        }
        FiberAndThread(std::function<void()>f,int thr):
        cb(f),thread(thr){
        }
        FiberAndThread(std::function<void()>*f ,int thr)://不拷贝 直接交换指针
        thread(thr){
            cb.swap(*f);
        }
        FiberAndThread(Fiber::ptr* f, int thr)
            :thread(thr) {
            fiber.swap(*f);
        }

        FiberAndThread()
            :thread(-1) {
        }
        void reset() {//重置
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
    struct Processor {
        typedef Mutex MutexType;
        int id = -1;
        Scheduler* scheduler = nullptr;
        // 只保护当前 P 的本地运行队列。
        // 每个 P 单独一把锁，避免本地投递/取任务都竞争全局锁。
        MutexType mutex;
        // 当前 P 的本地运行队列。
        // 所属线程从队头取任务，steal 从队尾偷任务，尽量减少冲突。
        std::list<FiberAndThread> run_queue;
        // 这些计数器对应 ProcessorStats。
        // 统计接口可能在工作线程运行时读取，所以这里使用原子类型。
        std::atomic<size_t> schedule_count{0};
        std::atomic<size_t> execute_count{0};
        std::atomic<size_t> local_execute_count{0};
        std::atomic<size_t> global_execute_count{0};
        std::atomic<size_t> steal_execute_count{0};
        std::atomic<size_t> global_pull_count{0};
        std::atomic<size_t> global_batch_count{0};
        std::atomic<size_t> steal_count{0};
        std::atomic<size_t> steal_batch_count{0};
        std::atomic<size_t> steal_attempt_count{0};
        std::atomic<size_t> steal_fail_count{0};
    };
    // 记录当前任务来源，run() 成功取到任务后据此更新对应统计。
    // 这个状态只属于本轮调度，不放进任务对象本身。
    enum class TaskSource {
        NONE,
        LOCAL,
        GLOBAL,
        STEAL
    };

    //协程调度启动（无锁）
    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        FiberAndThread ft(fc, thread);//不管是协程 还是函数都可以封装
        if(!ft.fiber && !ft.cb) {
            return false;
        }

        // GMP 投递策略：如果当前线程已经绑定到本调度器的 P，
        // 普通任务优先进入当前 P 的本地队列。
        // 外部线程投递的任务、指定线程任务继续进入全局队列，
        // 这样可以保留原来的指定线程语义。
        //
        // 常见的 YieldToReady()/timer/IO 恢复路径会尽量留在本地队列，
        // 外部提交的任务则通过全局队列被任意工作线程发现。
        Processor* processor = nullptr;
        if(thread == -1 && GetThis() == this && t_processor
                && t_processor->scheduler == this) {
            processor = t_processor;
        }

        if(processor) {
            Processor::MutexType::Lock lock(processor->mutex);
            processor->run_queue.push_back(ft);
            ++processor->schedule_count;
            return hasIdleThreads();
        }

        bool need_tickle = m_fibers.empty();
        m_fibers.push_back(ft);
        ++m_globalScheduleCount;
        return need_tickle || hasIdleThreads();
    }

    Processor* bindProcessor();
    bool popLocalTask(FiberAndThread& ft);
    bool popGlobalTask(FiberAndThread& ft, bool& tickle_me);
    bool stealTask(FiberAndThread& ft);
    bool processorQueuesEmpty();
private:
    /// Mutex
    MutexType m_mutex;
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    /// 全局待执行协程队列，外部投递和指定线程任务走这里
    /// 外部提交和指定线程任务会先进入这里。
    /// 工作线程后续会在安全的情况下把普通任务搬运到本地 P 队列。
    std::list<FiberAndThread> m_fibers;
    /// GMP 简化模型中的 P，每个 P 持有一个本地运行队列
    /// 这是本项目里的简化版 Go P：持有本地运行队列和 P 级统计。
    /// 当前版本不实现 Go runtime 的抢占、sysmon 等完整机制。
    std::vector<std::unique_ptr<Processor>> m_processors;
    /// 工作线程绑定 P 时使用的轮询游标。
    std::atomic<size_t> m_nextProcessor{0};
    /// 任务窃取选择目标 P 时使用的轮转游标。
    std::atomic<size_t> m_nextStealProcessor{0};
    std::atomic<size_t> m_globalScheduleCount{0};
    /// 当前线程绑定到本调度器的 P，没有绑定时为空。
    static thread_local Processor* t_processor;
    /// use_caller为true时有效, 调度协程
    Fiber::ptr m_rootFiber;
    /// 协程调度器名称
    std::string m_name;
protected:
    /// 协程下的线程id数组
    std::vector<int> m_threadIds;
    /// 线程数量
    size_t m_threadCount = 0;
    /// 工作线程数量
    std::atomic<size_t> m_activeThreadCount = {0};
    /// 空闲线程数量
    std::atomic<size_t> m_idleThreadCount = {0};
    /// 是否正在停止
    bool m_stopping = true;
    /// 是否自动停止
    bool m_autoStop = false;
    /// 主线程id(use_caller)
    int m_rootThread = 0;
};

}
