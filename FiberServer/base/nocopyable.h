#pragma once
namespace FiberServer{
    class Nocopyable{//禁止拷贝赋值的基类 子类继承禁止
public:
        Nocopyable()=default;
        ~Nocopyable()=default;
        Nocopyable(Nocopyable& o)=delete; 
        Nocopyable& operator=(Nocopyable& o)=delete;
    };
}