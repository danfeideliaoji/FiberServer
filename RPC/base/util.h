#pragma once
#include<pthread.h>
#include<stdint.h>
#include<time.h>
#include <cxxabi.h>
#include<string>
namespace RPC{
    pid_t  GetThreadId();//获得线程id
    uint64_t GetCurrentMS();//获得当前毫秒
    std::string Time2Str(time_t ts,const std::string &format);//将时间转为字符串
    time_t Str2Time(const std::string &str,const std::string &format);//将字符串转为时间
    template<typename T>
    const char* TypeToName(){//获得类型名
    // abi::__cxa_demangle 是 Linux/Unix 下 GCC/Clang 提供的系统函数
    // 专门用来将混淆后的名字 (mangled name) 还原成人类可读的名字 (demangled name)
    static const char* s_name = abi::__cxa_demangle(
        //使用静态 相当于本地缓存 提高性能
        typeid(T).name(),   // 1. 传入混淆的类型名
        nullptr,            // 2. 输出缓冲区 (nullptr 表示让函数自己 malloc 分配)
        nullptr,            // 3. 长度 (nullptr 表示不关心)
        nullptr             // 4. 状态 (nullptr 表示不关心成功与否)
    );
    return s_name;
    }
}
