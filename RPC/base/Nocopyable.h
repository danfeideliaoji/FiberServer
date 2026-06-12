#pragma once
namespace RPC{
    class Nocopyalbe{//禁止拷贝赋值的基类
public:
        Nocopyalbe()=default;
        ~Nocopyalbe()=default;
        Nocopyalbe(Nocopyalbe& o)=delete; 
        Nocopyalbe& operator=(Nocopyalbe& o)=delete;
    };
}