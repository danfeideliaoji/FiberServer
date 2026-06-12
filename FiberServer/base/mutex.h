#pragma once
#include <pthread.h>
#include <semaphore.h>
#include <cstdint>
#include "nocopyable.h"
namespace FiberServer{
//信号量
class Semaphore:public Nocopyable{
public:
    Semaphore(uint32_t count=0);
    ~Semaphore();
    void wait();//获取信号量
    void notify();//释放信号量
private:
    sem_t m_sem;
};

template <typename T>
class ScopedLockImpl //作用域锁
{ // 模版类RALL管理锁 相当于unique_lock
public:
    ScopedLockImpl(T &mutex):m_mutex(mutex)
    { // 构造时上锁
        m_mutex.lock();
        m_locked = true;
    }
    ~ScopedLockImpl()
    { // 析构时下锁
        unlock();
        m_locked = false;
    }
    void lock()
    {
        if (!m_locked)
        {
            m_mutex.lock();
            m_locked = true;
        }
    }
    void unlock()
    {
        if (m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T &m_mutex;    // 此类是管理锁的 所以要引用(锁也禁止拷贝)
    bool m_locked=false; // 是否拥有锁
};

template <typename T>
class WriteScopedLockImpl
{ //专门管理读锁 读写锁分离 便于一些几乎只读的提升性能 如配置系统
public:
    WriteScopedLockImpl(T& mutex):m_mutex(mutex)
    { // 构造时上锁
        m_mutex.Wrlock();
        m_locked = true;
    }
    ~WriteScopedLockImpl()
    { // 析构时下锁
        // m_mutex.unlock();
        unlock(); // 傻逼了 之前是上面那个 手动释放解耦会导致重复释放 找了半天才找出来
        m_locked = false;
    }
    void lock()
    {
        if (!m_locked)
        {
            m_mutex.Wrlock();
            m_locked = true;
        }
    }
    void unlock()
    {
        if (m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T &m_mutex;    // 此类是管理锁的 所以要引用(锁也禁止拷贝)
    bool m_locked=false; // 是否拥有锁
};

template <typename T>
class ReadScopedLockImpl
{ //专门管理读锁 读写锁分离 便于一些几乎只读的提升性能 如配置系统
public:
    ReadScopedLockImpl(T& mutex):m_mutex(mutex)
    { // 构造时上锁
        m_mutex.Rdlock();
        m_locked = true;
    }
    ~ReadScopedLockImpl()
    { // 析构时下锁
        // m_mutex.unlock();
        unlock();
        m_locked = false;
    }
    void lock()
    {
        if (!m_locked)
        {
            m_mutex.Rdlock();
            m_locked = true;
        }
    }
    void unlock()
    {
        if (m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    T &m_mutex;    // 此类是管理锁的 所以要引用(锁也禁止拷贝)
    bool m_locked=false; // 是否上锁
};

//互斥锁 同时只能一个访问
class Mutex:public Nocopyable{
public:
    typedef ScopedLockImpl<Mutex> Lock; // 方便使用
    Mutex(){
        pthread_mutex_init(&m_mutex ,nullptr);
    }
    ~Mutex(){
        pthread_mutex_destroy(&m_mutex);
    }
    void lock()
    {
        pthread_mutex_lock(&m_mutex);
    }
    void unlock()
    {
        pthread_mutex_unlock(&m_mutex);
    }
private:    
    pthread_mutex_t m_mutex;
};

//读写锁 可上读锁 写锁 提高某些情况的性能
class RWMutex:public Nocopyable{
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock; //设置类名 都大写符合命名规则    
    /// 局部写锁
    typedef WriteScopedLockImpl<RWMutex> WriteLock;
public:
    RWMutex(){
        pthread_rwlock_init(&m_rwmutex,nullptr);
    }
    ~RWMutex(){
        pthread_rwlock_destroy(&m_rwmutex);
    }
    void Rdlock(){
        pthread_rwlock_rdlock(&m_rwmutex);
    }
    void Wrlock(){
        pthread_rwlock_wrlock(&m_rwmutex);
    }
    void unlock(){
        pthread_rwlock_unlock(&m_rwmutex);
    }
private:
    pthread_rwlock_t m_rwmutex;
};
class Spinlock:public Nocopyable{//自旋锁 当阻塞时会不断的进行访问 而不是切换 减少线程切断耗时
//适合多锁 锁耗时少的 如log 减少耗时
public:
    typedef ScopedLockImpl<Spinlock> Lock;
    Spinlock(){
        pthread_spin_init(&m_spinmutex,0);
    }
    ~Spinlock(){
        pthread_spin_destroy(&m_spinmutex);
    }
    void lock(){
        pthread_spin_lock(&m_spinmutex);
    }
    void unlock(){
        pthread_spin_unlock(&m_spinmutex);
    }
private:
    pthread_spinlock_t  m_spinmutex;

};

}
