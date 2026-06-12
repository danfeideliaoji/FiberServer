#include "time.h"
#include "base/util.h"
#include "base/log.h"
namespace FiberServer{
bool Timer::Comparator::operator()(const Timer::ptr& lhs,
    const Timer::ptr& rhs)const{//比较定时器的智能指针的大小(按执行时间排序
        // 当右边大时为真 
        if(!lhs && !rhs){
            return false;
        }
        if(!lhs){
            return true;
        }
        if(!rhs){
            return false;
        }
        if(lhs->m_next<rhs->m_next){
            return true;
        }
        if(rhs->m_next<lhs->m_next){
            return false;
        }
        return lhs.get()<rhs.get();//地址比较 避免相等 set相等的只会有一个
    }

Timer::Timer(uint64_t ms,std::function<void()>cb,
    bool recurring,TimerManager* managger):m_recurring(recurring),
    m_ms(ms),
    m_cb(cb),
    m_manager(managger){
        m_next=GetCurrentMS()+m_ms;
    }

Timer::Timer(uint64_t next):m_next(next){

    }
bool Timer::cancel(){//取消定时器
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        //因为要从管理器中删除定时器 要加写锁
        if(m_cb){
            m_cb=nullptr;
            auto it=m_manager->m_timers.find(shared_from_this());//保证定时器被共享指针管理
            if(it == m_manager->m_timers.end()) {
            return false;
            }
            m_manager->m_timers.erase(it);
            return true;
        }
        return false;
}
bool Timer::refresh(){//刷新定时器
          TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
          if(!m_cb){
            return false;
          }
        auto it = m_manager->m_timers.find(shared_from_this());
        if(it == m_manager->m_timers.end()) {
        return false;
        }
        m_manager->m_timers.erase(it);
        m_next = FiberServer::GetCurrentMS() + m_ms;//更新时间  重新计时
        m_manager->m_timers.insert(shared_from_this());
        return true;
    }
bool Timer::reset(uint64_t ms,bool from_now){//重置定时器 from_now表示是否从当前时间开始计算
        if(ms==m_ms && !from_now){
            return true;
        }
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if(!m_cb) {
        return false;
        }
        auto it = m_manager->m_timers.find(shared_from_this());
        if(it == m_manager->m_timers.end()) {
        return false;
        }
        m_manager->m_timers.erase(it);
        uint64_t start = 0;
        if(from_now) {
            start = FiberServer::GetCurrentMS();
        } else {
            start = m_next - m_ms;//这里后面又加回来了 主要是为了统一
        }
        m_ms = ms;
        m_next = start + m_ms;
        m_manager->addTimer(shared_from_this(), lock);
        return true;
        }

TimerManager::TimerManager(){
            m_previouseTime = FiberServer::GetCurrentMS();
        }

TimerManager::~TimerManager() {
        }

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                                  ,bool recurring) {
        Timer::ptr timer(new Timer(ms, cb, recurring, this));
        RWMutexType::WriteLock lock(m_mutex);
        addTimer(timer, lock);
        return timer;
        }
        
void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {//将定时器添加到管理器中 里面不会再次上锁
        auto it = m_timers.insert(val).first;//insert返回的是pair<iterator,bool> 这样获得val迭代器 方便后续操作 
        bool at_front = (it == m_timers.begin()) && !m_tickled;
        if(at_front) {
        m_tickled = true;
        }
        lock.unlock();
        if(at_front) {//如果任务在最前面 还要特殊考虑
        onTimerInsertedAtFront();
        }
    }
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)//看任务是否还存在
    {
        std::shared_ptr<void> tmp = weak_cond.lock();
        if(tmp) {
            cb();
        }
    }

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
    ,std::weak_ptr<void> weak_cond,bool recurring) {
        return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
    }

uint64_t TimerManager::getNextTimer() {
        RWMutexType::ReadLock lock(m_mutex);
        m_tickled = false;
        if(m_timers.empty()) {
            return ~(0ull);//无定时器 返回最大值
        }
        const Timer::ptr& next = *(m_timers.begin());//常引用非常好的习惯 避免一些无法的引用的
        uint64_t now_ms =FiberServer::GetCurrentMS();//util里面
        if(now_ms >= next->m_next) {
            return 0;
        } else {
            return next->m_next - now_ms;
        }
    }

void TimerManager::listExpiredCb(std::vector<std::function<void()>>&cbs){
        uint64_t now_ms = FiberServer::GetCurrentMS();
        std::vector<Timer::ptr> expired;
        {
            RWMutexType::ReadLock lock(m_mutex);
            if(m_timers.empty()){
                return;
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        if(m_timers.empty()){
            return;
        }
        bool rollover  = detectClockRollover(now_ms);
        if(! rollover &&((*m_timers.begin())->m_next > now_ms)){
            return;
        }
        Timer::ptr now_timer(new Timer(now_ms));
        auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
        while(it!=m_timers.end() && (*it)->m_next == now_ms){
            ++it;
        }
        expired.insert(expired.begin(),m_timers.begin(),it);
        m_timers.erase(m_timers.begin(), it);
        cbs.reserve(expired.size());

        for(auto & timer : expired){
            cbs.push_back(timer->m_cb);
            if(timer->m_recurring){
                timer->m_next = now_ms + timer->m_ms;
                m_timers.insert(timer);
            }
            else{
                timer->m_cb = nullptr;
            }
        }
    }

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
    }

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if(now_ms < m_previouseTime &&
            now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}
}