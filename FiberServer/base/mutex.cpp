#include"mutex.h"
#include<stdexcept>
namespace FiberServer{
Semaphore::Semaphore(uint32_t count){
    if(sem_init(&m_sem,0,count)==-1){
        throw std::logic_error("semaphore init error");
    }
}
Semaphore::~Semaphore() {
    sem_destroy(&m_sem);
}
void Semaphore::wait(){
    if(sem_wait(&m_sem)){
        throw std::logic_error("semaphore wait error");
    }
}
void Semaphore::notify() {
    if(sem_post(&m_sem)) {
        throw std::logic_error("sem_post error");
    }
}



}