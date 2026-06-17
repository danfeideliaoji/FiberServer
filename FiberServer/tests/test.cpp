#include<string>
#include<vector>
#include<tuple>
#include<iostream>
#include<thread>
#include<atomic>
#include<cassert>
#include<cstring>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/socket.h>
#include<unistd.h>
#include "FiberServer/base/log.h"
#include "FiberServer/base/config.h"
#include "FiberServer/thread.h"
#include "FiberServer/base/macro.h"
#include "FiberServer/scheduler.h"
#include "FiberServer/fiber.h"
#include "FiberServer/hook.h"
#include "FiberServer/iomanager.h"
#include "FiberServer/net/address.h"
#include "FiberServer/util/hash_util.h"

struct SchedulerStatsSum {
    size_t local_scheduled = 0;
    size_t executed = 0;
    size_t local_executed = 0;
    size_t global_executed = 0;
    size_t steal_executed = 0;
    size_t global_pulled = 0;
    size_t global_batches = 0;
    size_t stolen = 0;
    size_t steal_batches = 0;
    size_t steal_attempts = 0;
    size_t steal_fails = 0;
};

static SchedulerStatsSum sumSchedulerStats(const FiberServer::Scheduler::SchedulerStats& stats) {
    SchedulerStatsSum sum;
    for(const auto& processor : stats.processors) {
        sum.local_scheduled += processor.schedule_count;
        sum.executed += processor.execute_count;
        sum.local_executed += processor.local_execute_count;
        sum.global_executed += processor.global_execute_count;
        sum.steal_executed += processor.steal_execute_count;
        sum.global_pulled += processor.global_pull_count;
        sum.global_batches += processor.global_batch_count;
        sum.stolen += processor.steal_count;
        sum.steal_batches += processor.steal_batch_count;
        sum.steal_attempts += processor.steal_attempt_count;
        sum.steal_fails += processor.steal_fail_count;
    }
    return sum;
}

void test_log_ime(){
    std::atomic<int> count{0};
    auto root=FIBER_LOG_NAME("test");
    root->addAppender(FiberServer::LogAppender::Type::FILE);
    auto time1=std::chrono::steady_clock::now();
    std::thread th1([&](){
      for(int i=0;i<50000;++i){
          FIBER_LOG_FATAL(root)<<count++;
      }
  });
   std::thread th2([&](){
      for(int i=0;i<50000;++i){
           FIBER_LOG_DEBUG(root)<<count++;
      }
  });
  th1.join();
  th2.join();
  auto dur=std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now()-time1);
  std::cout<<dur.count();
}

void test_mutex(){
    FiberServer::RWMutex mutex;
    {
    FiberServer::RWMutex::ReadLock lock(mutex);
    lock.unlock();
    }
    FiberServer::RWMutex::WriteLock lock(mutex);
    FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"hello world";
}
void test_config(){
    
    FiberServer::Config::LoadFromConfDir("/home/a/cppproject/FiberServer/config.txt");
    auto test=FIBER_LOG_NAME("test1");
    std::cout<<test->toYamlString()<<std::endl;//测试从配置文件里读
    FIBER_LOG_INFO(test)<<"this is a test"<<std::endl;
    auto v=FiberServer::Config::LookupBase("logs"); 
    std::cout<<v->toString()<<std::endl;//测试从logdefine转为字符串
    for(const auto &i:FiberServer::Config::GetDatas())
        FIBER_LOG_DEBUG(FIBER_LOG_ROOT())<<i.first<<" "<<i.second->toString();
}
void test_schedule(){
    FiberServer::Config::LoadFromConfDir("/home/a/cppproject/FiberServer/config.txt");
    FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::INFO);
    FiberServer::Scheduler sched(4,true,"test");
    FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"start fiber counts="<<FiberServer::Fiber::TotalFibers();
    sched.start();
    sched.schedule([](){
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"hello world ";
        FiberServer::Fiber::YieldToReady();
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"hello world again";
    });
    sleep(1);
     sched.schedule([](){
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<FiberServer::Thread::GetName()<<"  "
        <<FiberServer::Fiber::GetFiberId()<<"HELLO WORLD ";
        FiberServer::Fiber::YieldToReady();
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<FiberServer::Thread::GetName()<<"  "
        <<FiberServer::Fiber::GetFiberId()<<"HELLO WORLD AGAIN";
    });
    sleep(2);
    sched.stop();

    FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"test_schedule finished ;now fiber counts="<<FiberServer::Fiber::TotalFibers();
}

void test_gmp_scheduler(){
    FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::WARN);
    std::atomic<int> first_run{0};
    std::atomic<int> resumed{0};
    const int task_count = 200;

    // 测试从调用线程提交任务，第一轮任务会先进入全局队列。
    // 每个协程 YieldToReady() 后，会由工作线程重新投递，应该走本地 P 队列路径。
    FiberServer::Scheduler sched(4,false,"gmp_test");
    sched.start();
    for(int i = 0; i < task_count; ++i) {
        sched.schedule([&](){
            ++first_run;
            FiberServer::Fiber::YieldToReady();
            ++resumed;
        });
    }
    sched.stop();

    assert(first_run == task_count);
    assert(resumed == task_count);

    auto stats = sched.getStats();
    auto sum = sumSchedulerStats(stats);

    // 因为任务是从调用线程提交的，所以第一次执行来自全局队列。
    // YieldToReady() 之后由工作线程重新调度，应当贡献本地投递/本地执行统计。
    assert(stats.global_schedule_count >= (size_t)task_count);
    assert(sum.local_scheduled >= (size_t)task_count);
    assert(sum.executed >= (size_t)task_count * 2);
    assert(sum.executed == sum.local_executed + sum.global_executed + sum.steal_executed);
    assert(sum.global_pulled >= (size_t)task_count);
    assert(sum.global_batches > 0);

    std::cout << "gmp scheduler test passed: " << resumed.load()
              << " tasks, local_scheduled=" << sum.local_scheduled
              << ", executed=" << sum.executed
              << ", global_executed=" << sum.global_executed
              << ", local_executed=" << sum.local_executed
              << ", steal_executed=" << sum.steal_executed
              << ", global_pulled=" << sum.global_pulled
              << ", global_batches=" << sum.global_batches
              << ", stolen=" << sum.stolen
              << ", steal_batches=" << sum.steal_batches << std::endl;
}

void test_gmp_batch_stealing(){
    FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::WARN);
    const int task_count = 200;
    std::atomic<int> entered{0};
    std::atomic<int> completed{0};

    // 父任务会在一个 P 的本地队列里批量塞入子任务。
    // 其他工作线程需要从这个积压队列里窃取任务，测试才会出现 steal 统计。
    FiberServer::Scheduler sched(4,false,"gmp_steal_test");
    sched.start();
    sched.schedule([&](){
        for(int i = 0; i < task_count; ++i) {
            sched.schedule([&](){
                ++entered;
                while(entered.load() < task_count) {
                    FiberServer::Fiber::YieldToReady();
                }
                ++completed;
            });
        }
    });
    sched.stop();

    assert(entered == task_count);
    assert(completed == task_count);

    auto sum = sumSchedulerStats(sched.getStats());

    // 如果任务窃取失效，这个测试通常会卡住，
    // 或者虽然完成但 steal 相关统计保持为 0。
    assert(sum.stolen > 0);
    assert(sum.steal_batches > 0);
    assert(sum.steal_executed > 0);
    assert(sum.steal_attempts >= sum.steal_batches);

    std::cout << "gmp batch stealing test passed: stolen=" << sum.stolen
              << ", steal_batches=" << sum.steal_batches
              << ", steal_executed=" << sum.steal_executed
              << ", steal_attempts=" << sum.steal_attempts
              << ", steal_fails=" << sum.steal_fails << std::endl;
}

void test_hookAndiomanager(){
    FiberServer::Config::LoadFromConfDir("/home/a/cppproject/FiberServer/config.txt");
    // FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::INFO);
    FiberServer::set_hook_enable(true);
    FiberServer::IOManager iomanager(4,true,"test");
    iomanager.schedule([]{
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"hello world before sleep";
        sleep(1);
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"hello world after sleep";
    });
    FIBER_LOG_INFO(FIBER_LOG_ROOT())<<FiberServer::Thread::GetName()<<"  "
        <<FiberServer::Fiber::GetFiberId()<<"HELLO WORLD ";
}

void test_iomanager_sleep_timer(){
    FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::WARN);
    std::atomic<int> before_sleep{0};
    std::atomic<int> after_sleep{0};

    FiberServer::IOManager iomanager(2,false,"timer_test");
    iomanager.schedule([&](){
        ++before_sleep;
        usleep(20 * 1000);
        ++after_sleep;
    });
    iomanager.stop();

    assert(before_sleep == 1);
    assert(after_sleep == 1);

    auto stats = iomanager.getStats();
    size_t local_scheduled = 0;
    size_t executed = 0;
    // timer 回调会通过 IOManager 恢复睡眠中的协程。
    // GMP 改造后，这条恢复路径仍然应该进入 Scheduler 统计。
    for(const auto& processor : stats.processors) {
        local_scheduled += processor.schedule_count;
        executed += processor.execute_count;
    }
    assert(local_scheduled >= 1);
    assert(executed >= 2);

    std::cout << "iomanager sleep timer test passed: local_scheduled="
              << local_scheduled << ", executed=" << executed << std::endl;
}

void test_iomanager_socket_hook(){
    FIBER_LOG_ROOT()->setLevel(FiberServer::LogLevel::WARN);
    std::atomic<int> accepted{0};
    std::atomic<int> server_read{0};
    std::atomic<int> client_done{0};

    FiberServer::IOManager iomanager(2,false,"socket_test");
    iomanager.schedule([&](){
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(listen_fd >= 0);

        int on = 1;
        assert(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        assert(bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        assert(listen(listen_fd, 16) == 0);

        socklen_t addr_len = sizeof(addr);
        assert(getsockname(listen_fd, (sockaddr*)&addr, &addr_len) == 0);
        int port = ntohs(addr.sin_port);

        iomanager.schedule([&, port](){
            int client_fd = socket(AF_INET, SOCK_STREAM, 0);
            assert(client_fd >= 0);

            sockaddr_in peer;
            memset(&peer, 0, sizeof(peer));
            peer.sin_family = AF_INET;
            peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            peer.sin_port = htons(port);
            assert(connect(client_fd, (sockaddr*)&peer, sizeof(peer)) == 0);

            const char* request = "ping";
            assert(write(client_fd, request, 4) == 4);

            char response[8] = {0};
            assert(read(client_fd, response, 4) == 4);
            assert(memcmp(response, "pong", 4) == 0);
            ++client_done;
            close(client_fd);
        });

        sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int conn_fd = accept(listen_fd, (sockaddr*)&peer, &peer_len);
        assert(conn_fd >= 0);
        ++accepted;

        char request[8] = {0};
        assert(read(conn_fd, request, 4) == 4);
        assert(memcmp(request, "ping", 4) == 0);
        ++server_read;

        const char* response = "pong";
        assert(write(conn_fd, response, 4) == 4);
        while(client_done.load() == 0) {
            FiberServer::Fiber::YieldToReady();
        }
        close(conn_fd);
        close(listen_fd);
    });
    iomanager.stop();

    assert(accepted == 1);
    assert(server_read == 1);
    assert(client_done == 1);

    auto stats = iomanager.getStats();
    size_t local_scheduled = 0;
    size_t executed = 0;
    // accept/connect/read/write 会被 hook，并通过 epoll 事件恢复协程。
    // 这里验证新增的 local/global/steal 队列没有破坏 IO 唤醒路径。
    for(const auto& processor : stats.processors) {
        local_scheduled += processor.schedule_count;
        executed += processor.execute_count;
    }
    assert(local_scheduled >= 1);
    assert(executed >= 2);

    std::cout << "iomanager socket hook test passed: local_scheduled="
              << local_scheduled << ", executed=" << executed << std::endl;
}

void test_hook(){//一个有趣的测试 如果是sylar的源码必定段错误
     FiberServer::Config::LoadFromConfDir("/home/a/cppproject/FiberServer/config.txt");
     FiberServer::set_hook_enable(true);
     FiberServer::IOManager iomanager(1,true,"test");
     sleep(2); //这里调用sleep会非常有意思 会跳来跳去哈哈
     FIBER_LOG_INFO(FIBER_LOG_ROOT())<<111111111;
}
void test_address(){
     FiberServer::Config::LoadFromConfDir("/home/a/cppproject/FiberServer/config.txt");
     std::multimap<std::string,std::pair<FiberServer::Address::ptr,uint32_t>> res;
     FiberServer::Address::GetInterfaceAddresses(res);
     for(auto &i:res){
       FIBER_LOG_INFO(FIBER_LOG_ROOT())<<i.first<<": "<<*i.second.first<<" "<<i.second.second;
       auto i_IP=std::dynamic_pointer_cast<FiberServer::IPAddress>(i.second.first);
       uint32_t port=i.second.second;
       FIBER_LOG_INFO(FIBER_LOG_ROOT())<<i_IP->broadcastAddress(port)->toString();
       FIBER_LOG_INFO(FIBER_LOG_ROOT())<<i_IP->networdAddress(port)->toString();
       FIBER_LOG_INFO(FIBER_LOG_ROOT())<<i_IP->subnetMask(port)->toString();
     }
     auto addr=FiberServer::Address::LookupAny("baidu.com:https");
     if(addr){
        FIBER_LOG_INFO(FIBER_LOG_ROOT())<<*addr;
     }
    auto addrIP=std::dynamic_pointer_cast<FiberServer::IPAddress>(addr);
    
}

void test_hmac_vectors() {
    const std::string text = "The quick brown fox jumps over the lazy dog";
    const std::string key = "key";
    assert(FiberServer::hexstring_from_data(FiberServer::hmac_md5(text, key))
        == "80070713463e7749b90c2dc24911e275");
    assert(FiberServer::hexstring_from_data(FiberServer::hmac_sha1(text, key))
        == "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
    assert(FiberServer::hexstring_from_data(FiberServer::hmac_sha256(text, key))
        == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    std::cout << "hmac vector test passed" << std::endl;
}

int main(){
   FiberServer::Thread::SetName("main");
    // test_log_time();
    // test_mutex();
   test_hmac_vectors();
   test_gmp_scheduler();
   test_gmp_batch_stealing();
    // test_config();   
    // test_schedule(); 
   test_iomanager_sleep_timer();
   test_iomanager_socket_hook();
//    test_hookAndiomanager();
    //    test_hook();
    // test_address();
   return 0;
}
