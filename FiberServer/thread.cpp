#include "thread.h"
#include "base/log.h"
namespace FiberServer{
    static Logger::ptr g_logger=FIBER_LOG_NAME("system");
    static thread_local Thread* t_thread=nullptr;
    static thread_local std::string t_thread_name="UNKNOW";//线程名称 用线程局部变量
    //这样就不用传递thread指针

    Thread* Thread::GetThis(){ //获取当前线程对象指针
        // 学学规范的命名 静态函数首字母都大写 强调是类的函数
        return t_thread;
    }
    const std::string& Thread::GetName(){//获取当前线程名称
        return t_thread_name;
    }
    Thread::Thread(std::function<void()> cb,const std::string& name):
    m_cb(cb),m_name(name){
        if(name.empty()){
            m_name="UNKNOW";
        }
        int rt=pthread_create(&m_thread,nullptr,&Thread::run,this);
        if(rt){//如果创建线程失败
            FIBER_LOG_ERROR(g_logger)<<"pthread_create thread fail,rt="<<rt
            <<",name="<<name;
            throw std::logic_error("pthread_create error");
        }
        m_semaphore.wait();//等待子线程运行完成后再返回
    }
    Thread::~Thread(){
        if(m_thread){
            pthread_detach(m_thread);//分离线程 
        }
    }
    void Thread::SetName(const std::string& name){//设置当前线程名字
        if(name.empty()) {
        return;
        }
        if(t_thread) {
        t_thread->m_name = name;
        }
        t_thread_name = name;
    }
    void Thread::join(){
        if(m_thread){
            int rt=pthread_join(m_thread,nullptr);
            if(rt){
                FIBER_LOG_ERROR(g_logger)<<"pthread_join thread fail,rt="
                <<rt<<",name="<<m_name;
                throw std::logic_error("pthread_join error");
            }
            m_thread=0;
        }
    }
    void* Thread::run(void * arg){
        t_thread=static_cast<Thread*>(arg);
        if(t_thread){
            t_thread_name=t_thread->m_name;
            t_thread->m_id=FiberServer::GetThreadId();
            pthread_setname_np(pthread_self(),t_thread_name.substr(0,16).c_str());
            std::function<void()> cb;
            cb.swap(t_thread->m_cb);//交出所有权 避免循环引用
            t_thread->m_semaphore.notify();//通知主线程创建成功
            cb();//执行线程函数  
        }
        return nullptr;
    }
}