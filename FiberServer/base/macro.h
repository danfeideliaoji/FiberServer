#pragma once
#include<cassert>
#include"log.h"
#include"util.h"

#define FIBER_LIKELY(x) __builtin_expect(!!(x),1)
//LIKCLY 宏的封装, 告诉编译器优化,条件大概率成立

#define FIBER_UNLIKELY(x) __builtin_expect(!!(x),0)
// LIKCLY 宏的封装, 告诉编译器优化,条件大概率不成立

#define FIBER_ASSERT(x)\
    if(!(x)){\
        FIBER_LOG_ERROR(FIBER_LOG_ROOT())<<"ASSERTION: " #x\
        <<"\nbacktrace:\n"\
        <<FiberServer::BacktraceToString(100,0,"    ");\
        assert(x);\
    }
    
#define FIBER_ASSERT2(x,info)\
    if(!(x)){\
        FIBER_LOG_ERROR(FIBER_LOG_ROOT())<<"ASSERTION: " #x\
        <<"\n"<<info\
        <<"\nbacktrace:\n"\
        <<FiberServer::BacktraceToString(100,0,"    ");\
        assert(x);\
    }
