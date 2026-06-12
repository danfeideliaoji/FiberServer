#include "util.h"
#include<pthread.h>
#include<cstring>
#include<unistd.h>
#include<chrono>
#include<sys/syscall.h>
namespace RPC{
    static thread_local pid_t t_thread_id = 0;
    pid_t GetThreadId(){
        if(t_thread_id==0){
            t_thread_id=syscall(SYS_gettid);
        }
        return t_thread_id;
    }
    uint64_t GetCurrentMS(){
        using namespace std::chrono;
        auto now=system_clock::now();
        auto ms=duration_cast<milliseconds>(now.time_since_epoch());
        return ms.count();
    }
    //时间转字符串
    std::string Time2Str(time_t ts,std::string &format){
        struct tm tm;
        localtime_r(&ts,&tm);
        char buf[64];
        strftime(buf,sizeof(buf),format.c_str(),&tm);
        return buf;
    }
    //字符串转时间
    time_t Str2Time(const std::string &str,const std::string &format){
        struct tm tm;
        memset(&tm,0,sizeof(tm));
        strptime(str.c_str(),format.c_str(),&tm);
        return mktime(&tm);
    }
}