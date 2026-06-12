#include "scheduler.h"
#include "base/log.h"
#include "base/macro.h"
#include "hook.h"
#ifndef FIBERSERVER_USE_SOCI
#include "FiberServer/db/mysql.h"
#endif
namespace FiberServer{
    static Logger::ptr g_logger=FIBER_LOG_NAME("system");

    //这里使用裸指针而不是共享指针 我思考有两方面问题 一方面是性能问题吧 高频切换 共享指针计数影响性能
    //另一方面是是这里只负责观察 不负责保存 有其他的共享指针保存 这里保存的话可能导致循环引用(c++的指针好复杂)
    static thread_local Scheduler* t_scheduler=nullptr;//当前的调度器指针

    static thread_local Fiber* t_scheduler_fiber=nullptr;//非常非常关键的变量
    thread_local Scheduler::Processor* Scheduler::t_processor=nullptr;
    //代表着当前线程主协程    对于主线程这个协程要绑定run 切换时回到其上下文
    //对于子线程 则不用绑定 代表当前上下文
    
    Scheduler::Scheduler(size_t threads,bool use_caller,const 
    std::string& name):m_name(name){
 //  use_caller表示stop时主线程是否也参与执行协程任务
//   这个变量非常关键 逻辑很多也很绕 后面很多地方都是为了这个变量写的
        FIBER_ASSERT(threads>0);
        if(use_caller){ //主协程参与
            FiberServer::Thread::SetName(m_name);
            Fiber::ptr fiber=Fiber::GetThis();
            --threads;//主线程调用stop后也会参与 所以要减1
            FIBER_ASSERT(GetThis()==nullptr);
            t_scheduler=this;
            m_rootFiber.reset(new Fiber([this]{
                run();
            },0,true));//这个要绑定run 原因在解释t_sheduler_fiber里已解释
            
            t_scheduler_fiber=m_rootFiber.get();
            
            m_rootThread=FiberServer::GetThreadId();
            
            m_threadIds.push_back(m_rootThread);
        }
        else{
            m_rootThread=-1;//主线程不参与的话 就-1代表没有 
        }
        m_threadCount=threads;
        size_t processor_count = m_threadCount + (m_rootThread == -1 ? 0 : 1);
        if(processor_count == 0) {
            processor_count = 1;
        }
        m_processors.reserve(processor_count);
        for(size_t i = 0; i < processor_count; ++i) {
            std::unique_ptr<Processor> processor(new Processor);
            processor->id = (int)i;
            processor->scheduler = this;
            m_processors.push_back(std::move(processor));
        }
    }
    Scheduler::~Scheduler(){
        FIBER_ASSERT(m_stopping);
        if(GetThis()==this){
            t_scheduler=nullptr;
        }
    }
    Scheduler* Scheduler::GetThis() {
    return t_scheduler;
    }
    Fiber* Scheduler::GetMainFiber() {//返回主协程
    return t_scheduler_fiber;
    }
    void Scheduler::start(){//启动协程调度器
        MutexType::Lock lock(m_mutex);
        if(!m_stopping){
            return;
        }
        m_stopping=false;
        FIBER_ASSERT(m_threads.empty());
        m_threads.resize(m_threadCount);
        for(size_t i=0;i<m_threadCount;++i){
            m_threads[i].reset(new Thread(
                [this](){
                    run();
                },m_name+"_"+std::to_string(i)));
            m_threadIds.push_back(m_threads[i]->getId());
        }
    }
    void Scheduler::stop(){// 停止协程调度器
    FIBER_LOG_INFO(g_logger)<<"start stop scheduler!";
    m_autoStop=true;
    if(m_rootFiber&&m_threadCount==0&&(m_rootFiber->getState()==
    Fiber::State::TERM||m_rootFiber->getState()==Fiber::State::INIT)){
            FIBER_LOG_INFO(g_logger)<<this<<" stopped";
            m_stopping = true;
    }
    if(stopping()){
            return ;
    }
 
    //下面是为了保证只有主线程可以调用stop 子线程不能调用
    if(m_rootThread != -1) {
        FIBER_ASSERT(GetThis() == this);
    } else {
        FIBER_ASSERT(GetThis() != this);
    }

    m_stopping = true;
    for(size_t i = 0; i < m_threadCount; ++i) {
        tickle();//唤醒线程
    }

    if(m_rootFiber) { //避免待会主协程阻塞
        tickle();
    }

    if(m_rootFiber) {
        if(!stopping()) {
            m_rootFiber->call();//执行主协程 这里一定是call
            //这样把当前上下文存到fiber的t_threadFiber run（被CallerMainFunc封装）执行完 调用back 又回到主线程
        }
    }
    std::vector<Thread::ptr> thrs; 
    //先swap到本地 再join 避免死锁 因为这里上锁 线程也会上锁
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    FIBER_LOG_INFO(g_logger)<< " start";
    for(auto& i : thrs) {
        i->join();
    }
    FIBER_LOG_INFO(g_logger)<< " stopped";
}

void Scheduler::setThis() {
    t_scheduler = this;
    }

Scheduler::SchedulerStats Scheduler::getStats() {
    SchedulerStats stats;
    stats.name = m_name;
    stats.global_schedule_count = m_globalScheduleCount.load();
    stats.active_thread_count = m_activeThreadCount.load();
    stats.idle_thread_count = m_idleThreadCount.load();

    MutexType::Lock lock(m_mutex);
    stats.global_queue_size = m_fibers.size();
    stats.processors.reserve(m_processors.size());
    for(auto& processor : m_processors) {
        ProcessorStats processor_stats;
        Processor::MutexType::Lock processor_lock(processor->mutex);
        processor_stats.id = processor->id;
        processor_stats.queue_size = processor->run_queue.size();
        processor_stats.schedule_count = processor->schedule_count.load();
        processor_stats.execute_count = processor->execute_count.load();
        processor_stats.local_execute_count = processor->local_execute_count.load();
        processor_stats.global_execute_count = processor->global_execute_count.load();
        processor_stats.steal_execute_count = processor->steal_execute_count.load();
        processor_stats.global_pull_count = processor->global_pull_count.load();
        processor_stats.global_batch_count = processor->global_batch_count.load();
        processor_stats.steal_count = processor->steal_count.load();
        processor_stats.steal_batch_count = processor->steal_batch_count.load();
        processor_stats.steal_attempt_count = processor->steal_attempt_count.load();
        processor_stats.steal_fail_count = processor->steal_fail_count.load();
        stats.processors.push_back(processor_stats);
    }
    return stats;
}

std::ostream& Scheduler::dump(std::ostream& os) {
    SchedulerStats stats = getStats();
    os << "[Scheduler name=" << stats.name
       << " global_queue=" << stats.global_queue_size
       << " global_scheduled=" << stats.global_schedule_count
       << " active_threads=" << stats.active_thread_count
       << " idle_threads=" << stats.idle_thread_count
       << " processors=" << stats.processors.size()
       << "]";
    for(const auto& processor : stats.processors) {
        os << "\n  [P id=" << processor.id
           << " queue=" << processor.queue_size
           << " local_scheduled=" << processor.schedule_count
           << " executed=" << processor.execute_count
           << " local_executed=" << processor.local_execute_count
           << " global_executed=" << processor.global_execute_count
           << " steal_executed=" << processor.steal_execute_count
           << " global_pulled=" << processor.global_pull_count
           << " global_batches=" << processor.global_batch_count
           << " stolen=" << processor.steal_count
           << " steal_batches=" << processor.steal_batch_count
           << " steal_attempts=" << processor.steal_attempt_count
           << " steal_fails=" << processor.steal_fail_count
           << "]";
    }
    return os;
}

Scheduler::Processor* Scheduler::bindProcessor() {
    // 为当前工作线程绑定一个 P，绑定关系在本轮 run 循环内保持稳定。
    // 稳定绑定后，schedule() 可以通过 thread_local 很快找到本地 P 队列。
    //
    // 这里是简化版 M:P 绑定：线程运行期间不会迁移到其他 P。
    // 这样模型更容易理解，负载均衡交给全局队列搬运和任务窃取处理。
    if(t_processor && t_processor->scheduler == this) {
        return t_processor;
    }
    if(m_processors.empty()) {
        return nullptr;
    }
    size_t index = m_nextProcessor++ % m_processors.size();
    t_processor = m_processors[index].get();
    return t_processor;
}

bool Scheduler::popLocalTask(FiberAndThread& ft) {
    // 快路径：工作线程优先消费自己绑定 P 的本地队列，
    // 不需要先竞争调度器全局锁，这是 GMP 化后最主要的局部性收益。
    //
    // READY 协程、IO/timer 恢复的协程通常会在工作线程内再次 schedule()，
    // 因此大部分后续执行可以走本地队列，减少全局队列锁竞争。
    Processor* processor = t_processor;
    if(!processor || processor->scheduler != this) {
        return false;
    }

    Processor::MutexType::Lock lock(processor->mutex);
    while(!processor->run_queue.empty()) {
        ft = processor->run_queue.front();
        processor->run_queue.pop_front();
        // 如果协程仍是 EXEC，说明它当前不可恢复执行。
        // 这里跳过这个旧队列项，继续寻找下一个可运行任务。
        if(ft.fiber && ft.fiber->getState() == Fiber::State::EXEC) {
            ft.reset();
            continue;
        }
        return true;
    }
    return false;
}

bool Scheduler::popGlobalTask(FiberAndThread& ft, bool& tickle_me) {
    // 全局队列是外部任务和指定线程任务的兜底入口。
    // 当前 P 能取到普通任务时，会顺带搬运一小批任务到本地队列，
    // 这样后续循环可以走更便宜的本地队列路径。
    //
    // 第一个可运行任务通过 ft 立即返回给当前线程执行；
    // 额外搬运的普通任务进入当前 P 本地队列，留给后续本地调度。
    Processor* processor = t_processor && t_processor->scheduler == this ? t_processor : nullptr;
    std::list<FiberAndThread> batch;
    bool got_task = false;
    {
        MutexType::Lock lock(m_mutex);
        auto it = m_fibers.begin();
        // 最多搬运当前可见全局队列的大约一半。
        // 这样既能摊薄全局锁开销，又避免某个 P 一次拿走全部任务。
        size_t batch_target = m_fibers.size() / 2;
        if(batch_target == 0) {
            batch_target = 1;
        }

        while(it != m_fibers.end()) {
            if(it->thread != -1 && it->thread != FiberServer::GetThreadId()) {
                // 指定线程任务必须由目标线程执行。
                // 当前线程不能执行时保留在全局队列，并唤醒其他工作线程。
                ++it;
                tickle_me = true;
                continue;
            }

            FIBER_ASSERT(it->fiber || it->cb);
            if(it->fiber && it->fiber->getState() == Fiber::State::EXEC) {
                ++it;
                continue;
            }

            ft = *it;
            m_fibers.erase(it++);
            got_task = true;

            if(ft.thread == -1 && processor) {
                while(it != m_fibers.end() && batch.size() + 1 < batch_target) {
                    if(it->thread != -1) {
                        // 指定线程任务不能批量搬进当前 P 本地队列，
                        // 否则目标线程可能再也看不到这个任务。
                        ++it;
                        tickle_me = true;
                        continue;
                    }
                    if(it->fiber && it->fiber->getState() == Fiber::State::EXEC) {
                        ++it;
                        continue;
                    }
                    batch.push_back(*it);
                    it = m_fibers.erase(it);
                }
            }
            tickle_me |= it != m_fibers.end();
            break;
        }
    }

    if(got_task) {
        if(processor) {
            // 统计包含立即返回的 ft 任务和额外搬运进本地队列的任务。
            // global_batch_count 表示搬运操作次数，不是任务数量。
            processor->global_pull_count.fetch_add(batch.size() + 1);
            ++processor->global_batch_count;
            if(!batch.empty()) {
                Processor::MutexType::Lock processor_lock(processor->mutex);
                processor->run_queue.splice(processor->run_queue.end(), batch);
            }
        }
        return true;
    }
    return false;
}

bool Scheduler::stealTask(FiberAndThread& ft) {
    // 任务窃取用来处理某个 P 堆积任务、其他 P 空闲的情况。
    // 偷到的一批任务中，第一个任务立即返回给当前线程执行，
    // 剩余任务追加到当前 P 本地队列，后续按本地任务执行。
    //
    // 这里有意保持简单：不实现 Go runtime 的抢占、runnext、sysmon。
    // 当前逻辑只负责在多个工作线程的本地队列之间做负载均衡。
    Processor* current = t_processor;
    if(!current || current->scheduler != this) {
        return false;
    }
    ++current->steal_attempt_count;
    if(m_processors.size() <= 1) {
        ++current->steal_fail_count;
        return false;
    }

    // 轮转偷取起点，避免所有空闲线程每次都先抢同一个 P。
    size_t start = m_nextStealProcessor++ % m_processors.size();
    for(size_t offset = 0; offset < m_processors.size(); ++offset) {
        Processor* victim = m_processors[(start + offset) % m_processors.size()].get();
        if(victim == current) {
            continue;
        }

        std::list<FiberAndThread> stolen;
        {
            Processor::MutexType::Lock lock(victim->mutex);
            // 每次偷取 victim 队列的大约一半。
            // 只偷一个任务负载均衡太慢；全部偷走又可能让 victim 饿死。
            size_t steal_target = victim->run_queue.size() / 2;
            if(steal_target == 0) {
                steal_target = 1;
            }

            auto it = victim->run_queue.end();
            while(it != victim->run_queue.begin() && stolen.size() < steal_target) {
                --it;
                auto current_it = it;
                if(current_it->thread != -1) {
                    // 本地队列理论上主要是普通任务。
                    // 这里保留保护，确保 steal 不破坏指定线程语义。
                    continue;
                }
                if(current_it->fiber && current_it->fiber->getState() == Fiber::State::EXEC) {
                    continue;
                }

                stolen.push_back(*current_it);
                it = victim->run_queue.erase(current_it);
            }
        }

        if(!stolen.empty()) {
            ft = stolen.front();
            stolen.pop_front();
            size_t stolen_count = stolen.size() + 1;
            // steal_count 统计任务数，steal_batch_count 统计成功偷取批次数。
            // 第一个偷到的任务会立即执行，剩余任务放入当前 P 本地队列。
            current->steal_count.fetch_add(stolen_count);
            ++current->steal_batch_count;

            if(!stolen.empty()) {
                Processor::MutexType::Lock current_lock(current->mutex);
                current->run_queue.splice(current->run_queue.end(), stolen);
            }
            return true;
        }
    }
    ++current->steal_fail_count;
    return false;
}

bool Scheduler::processorQueuesEmpty() {
    for(auto& processor : m_processors) {
        Processor::MutexType::Lock lock(processor->mutex);
        if(!processor->run_queue.empty()) {
            return false;
        }
    }
    return true;
}
    
void Scheduler::run() {//协程调度函数
    FIBER_LOG_DEBUG(g_logger) << m_name << " run";
    set_hook_enable(true); //开启当前线程hook 关键 一定要有!!!!!!!
    setThis();
    bindProcessor();
#ifndef FIBERSERVER_USE_SOCI
    MySQLThreadIniter mysql_initer;//mysql线程初始化 关键 一定要有!!!!!!!
#endif
    if(FiberServer::GetThreadId() != m_rootThread){//如果不是主线程的run
         //设置当前的主协程 这里不用绑定函数 因为线程一直在run 
         //主协程主要是为了保存上下文 swapin swapout
        t_scheduler_fiber = Fiber::GetThis().get();
    }
    //空闲协程 当没任务时线程会跑 会阻塞 减少cpu消耗 同时兼具停止run的作用 
    //当空闲协程执行完(stoppinng()为真)表明该break了
    Fiber::ptr idle_fiber(new Fiber([this]{idle();}));

    FiberAndThread ft;//接受任务
    Fiber::ptr cb_fiber;//当任务是函数封装协程 第一次需要专门申请协程栈 后面都重复利用

    while(true) {//run 子线程一直循环
        ft.reset();
        bool tickle_me = false;//是否要tickle 唤醒空闲 阻塞线程
        bool is_active = false;//是否有活跃任务
        TaskSource task_source = TaskSource::NONE;
        // 简化版 GMP 调度顺序：
        // 当前 P 本地队列 -> 全局队列 -> 从其他 P 窃取任务 -> 空闲等待。
        //
        // 顺序不能随便调：
        // 1. local 优先保证刚恢复/刚 yield 的协程尽量留在当前工作线程附近；
        // 2. global 让外部提交的任务能公平进入调度系统；
        // 3. steal 避免某个 P 有积压时其他工作线程一直空闲。
        if(popLocalTask(ft)) {
            task_source = TaskSource::LOCAL;
        } else if(popGlobalTask(ft, tickle_me)) {
            task_source = TaskSource::GLOBAL;
        } else if(stealTask(ft)) {
            task_source = TaskSource::STEAL;
        }

        if(task_source != TaskSource::NONE) {
            if(t_processor && t_processor->scheduler == this) {
                // 执行来源统计统一放在 run 循环里做。
                // 这样不管任务是 Fiber 还是 callback，每个成功取到的任务都只统计一次。
                ++t_processor->execute_count;
                switch(task_source) {
                    case TaskSource::LOCAL:
                        ++t_processor->local_execute_count;
                        break;
                    case TaskSource::GLOBAL:
                        ++t_processor->global_execute_count;
                        break;
                    case TaskSource::STEAL:
                        ++t_processor->steal_execute_count;
                        break;
                    default:
                        break;
                }
            }
            ++m_activeThreadCount;
            is_active = true;
        }

        if(tickle_me) {
            // 全局队列里可能还有可执行任务，常见情况是任务指定了其他线程。
            // 这里唤醒空闲工作线程，让正确的线程有机会取到它。
            tickle();
        }
        //任务是协程
        if(ft.fiber && (ft.fiber->getState() != Fiber::State::TERM
                        && ft.fiber->getState() != Fiber::State::EXCEPT)) {
            ft.fiber->swapIn();//执行协程
            --m_activeThreadCount;

            if(ft.fiber->getState() == Fiber::State::READY) {
                schedule(ft.fiber);//为ready说明还有任务
            }
            else if(ft.fiber->getState() != Fiber::State::TERM
                    && ft.fiber->getState() != Fiber::State::EXCEPT) {
                //协程让出执行(YieldToHold) 保持EXEC防止竞态 这里设回HOLD
                ft.fiber->m_state = Fiber::State::HOLD;
            }
            ft.reset();//调度器不负责保存 对于完成的重置栈空间
        }
        //任务是函数 先封装成协程
        else if(ft.cb) {
            if(cb_fiber) {//已经有协程栈 重复利用
                cb_fiber->reset(ft.cb);
            } else { //没有协程栈就申请
                cb_fiber.reset(new Fiber(ft.cb));
            }
            ft.reset();//有协程的保存了 置空
            cb_fiber->swapIn();//执行
            --m_activeThreadCount;//执行完
            if(cb_fiber->getState() == Fiber::State::READY) {//如果还要执行
                schedule(cb_fiber);
                cb_fiber.reset();
            } else if(cb_fiber->getState() == Fiber::State::EXCEPT
                    || cb_fiber->getState() == Fiber::State::TERM) {
                cb_fiber->reset(nullptr);
            } else {
                //协程让出执行 设回HOLD后再释放共享指针
                cb_fiber->m_state = Fiber::State::HOLD;
                cb_fiber.reset();
            }
        }
        else {//下面是没取到任务的情况
            if(is_active) {
                --m_activeThreadCount;
                continue;
            }
            if(idle_fiber->getState() == Fiber::State::TERM) {//这里非常关键 用idle协程判断
                //是否要停止run 当stopping()为真时idle协程执行完 设置TERM
                FIBER_LOG_INFO(g_logger) << "idle fiber term";
                break;
            }

            ++m_idleThreadCount;
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if(idle_fiber->getState() != Fiber::State::TERM
                    && idle_fiber->getState() != Fiber::State::EXCEPT) {
                idle_fiber->m_state = Fiber::State::HOLD;
            }
        }
    }
    if(t_processor && t_processor->scheduler == this) {
        t_processor = nullptr;
    }
    // FIBER_LOG_INFO(g_logger) <<m_name<<" run end";
    }
    
void Scheduler::tickle() {//通知协程调度器有任务了
    FIBER_LOG_INFO(g_logger) << "tickle";
    }
    
void Scheduler::idle() {//协程调度器空闲时执行的函数
    //这里原本的循环一次就回去了 当满足stop时 则设置好状态切回 run里根据状态判断是否break
    FIBER_LOG_INFO(g_logger) << "idle";
    while(!stopping()) {
        FiberServer::Fiber::YieldToHold();
    }
    //执行完fiber会给其收尾

    }
    
bool Scheduler::stopping() {//返回是否可以停止
    MutexType::Lock lock(m_mutex);//公用变量 要加锁
    bool can_stop = m_autoStop && m_stopping && m_activeThreadCount == 0;
    bool global_empty = m_fibers.empty();
    lock.unlock();
    // 加入每个 P 的本地队列后，只检查全局队列已经不够。
    // 调度器只有在没有活跃任务，并且全局队列、本地队列都清空后才能停止。
    return can_stop && global_empty && processorQueuesEmpty();
    }
}
