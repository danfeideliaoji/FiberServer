#pragma once
#include<pthread.h>
#include<functional>
#include<memory>
#include<string>
#include "base/nocopyable.h"
#include "base/mutex.h"
namespace FiberServer{
    class Thread:public Nocopyable{
        public:
            typedef std::shared_ptr<Thread> ptr;
            Thread(std::function<void()> cb, const std::string& name);
            ~Thread();
            const std::string& getName() const{return m_name;}
            pid_t getId() const{return m_id;}
            static Thread* GetThis();
            void join();
            static void SetName(const std::string& name);//设置线程名字 也可以用于主线程
            static const std::string& GetName();//返回线程名字
        private:
            static void* run(void* arg);////线程执行函数 巧妙的解决了cAPI不能传c++成员变量的问题
            //成员函数默认有一个参数来传this指针 而pthread的执行函数参数要是 void*(void*)所以
        private:
            pid_t m_id=-1;
            std::string m_name;
            pthread_t m_thread;
            std::function<void()> m_cb;//回调函数
            Semaphore m_semaphore;    
    };
}
