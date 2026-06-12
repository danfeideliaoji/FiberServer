#include "application.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/config.h"
#include <cstdlib>

namespace FiberServer {

std::atomic<bool> Application::g_running{true};
static ConfigVar<uint64_t>::ptr g_workerThreads = Config::Lookup<uint64_t>(
    "server.worker_threads", 5, "http IOManager worker threads");

Application::Application() {
    m_mainThreadId = std::this_thread::get_id();
}

Application::~Application() {
    stop();
}

void Application::init() {
    // 主线程也需要 MySQL 线程初始化！
    // MySQLThreadIniter mysql_initer;

    const char* config_path = std::getenv("FIBER_CONFIG");
    Config::LoadFromConfDir(config_path && *config_path ? config_path : "./config.txt");
    FIBER_LOG_ROOT()->setLevel(LogLevel::WARN);
    
    std::signal(SIGINT, [](int){g_running = false;});
    std::signal(SIGTERM, [](int){g_running = false;});

}

void Application::run() {
    init();

    uint64_t worker_threads = g_workerThreads->getValue();
    const char* worker_threads_env = std::getenv("FIBER_WORKER_THREADS");
    if(worker_threads_env && *worker_threads_env) {
        char* end = nullptr;
        uint64_t env_value = std::strtoull(worker_threads_env, &end, 10);
        if(end && *end == '\0' && env_value > 0) {
            worker_threads = env_value;
        }
    }
    if(worker_threads == 0) {
        worker_threads = 1;
    }
    m_iom = std::make_unique<IOManager>(worker_threads, true, "http");
    m_chunkManager = std::make_unique<ChunkManager>();//添加定时器
    m_iom->schedule([this]() {
        auto addr = Address::LookupAny("0.0.0.0:8080");
        if (!addr) {
            FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "LookupAny address fail";
            return;
        }
        m_server = std::make_shared<http::HttpServer>(
            true,
            IOManager::GetThis(),
            IOManager::GetThis());
        m_server->setName("fiber_http");

        std::vector<Address::ptr> addrs{addr};
        std::vector<Address::ptr> fails;
        if (!m_server->bind(addrs, fails)) {
            for (auto& i : fails) {
                FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "bind fail: " << i->toString();
            }
            return;
        }
        
        if (!m_server->start()) {
            FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "server start fail";
            return;
        }
        
        FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "http server start at " << addr->toString()
            << " (Ctrl+C to stop)";

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        m_server->stop();
        FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "http server stopped";
    });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // std::cout<<Logger::ptr(FIBER_LOG_NAME("system"))->toYamlString()<<std::endl;
    // std::cout<<Logger::ptr(FIBER_LOG_NAME("mysql"))->toYamlString()<<std::endl;
}

void Application::stop() {
    g_running = false;
    m_chunkManager.reset();
    m_iom->stop();
}

void Application::signalHandler(int) {
    g_running = false;
}

} // namespace FiberServer
