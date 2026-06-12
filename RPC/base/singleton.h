#pragma once
#include<memory>
template<typename T,typename X=void,int N=0>
class Singleton{//单例模式模板类 T为单例类 X为T的构造函数参数类型 N为参数个数
public:
    static T* GetInstance(){
        static T v;
        return &v;
    }
};