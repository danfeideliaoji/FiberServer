#include<string>
#include<vector>
#include<tuple>
#include<iostream>
#include<thread>
#include<atomic>
#include "RPC/log.h"
#include "RPC/config.h"
void test_log_time(){
    std::atomic<int> count{0};
    auto root=RPC_LOG_NAME("test");
    root->addAppender(RPC::LogAppender::Type::FILE);
    auto time1=std::chrono::steady_clock::now();
    std::thread th1([&](){
      for(int i=0;i<50000;++i){
          RPC_LOG_FATAL(root)<<count++;
      }
  });
   std::thread th2([&](){
      for(int i=0;i<50000;++i){
           RPC_LOG_DEBUG(root)<<count++;
      }
  });
  th1.join();
  th2.join();
  auto dur=std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now()-time1);
  std::cout<<dur.count();
}
void test_config(){
    
    RPC::Config::LoadFromConfDir("/home/a/cppproject/RPC/config.txt");
    auto test=RPC_LOG_NAME("test1");
    std::cout<<test->toYamlString();//测试从配置文件里读
    RPC_LOG_INFO(test)<<"this is a test";
    auto v=RPC::Config::LookupBase("logs"); 
    std::cout<<v->toString()<<std::endl;//测试从logdefine转为字符串
}
int main(){
    // test_log_time();
    test_config();   
   return 0;
}