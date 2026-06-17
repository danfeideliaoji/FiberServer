#include "application.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/config.h"
#include "FiberServer/db/db_executor.h"
#include <cstdlib>
#include <string>

namespace FiberServer {

std::atomic<bool> Application::g_running{true};
static ConfigVar<uint64_t>::ptr g_workerThreads = Config::Lookup<uint64_t>(
    "server.worker_threads", 5, "http IOManager worker threads");
static ConfigVar<bool>::ptr g_perfLogEnabled = Config::Lookup<bool>(
    "perf.log_enabled", true, "enable per-request performance logs");

Application::Application() {
    m_mainThreadId = std::this_thread::get_id();
}

Application::~Application() {
    stop();
}

void Application::init() {

    const char* config_path = std::getenv("FIBER_CONFIG");
    Config::LoadFromConfDir(config_path && *config_path ? config_path : "docker/config.docker.yml");
    FIBER_LOG_ROOT()->setLevel(LogLevel::WARN);
    auto perf_logger = FIBER_LOG_NAME("perf");
    bool perf_log_enabled = g_perfLogEnabled->getValue();
    const char* perf_log_env = std::getenv("FIBER_PERF_LOG");
    if(perf_log_env && *perf_log_env) {
        std::string val = perf_log_env;
        perf_log_enabled = !(val == "0" || val == "false" || val == "FALSE" || val == "off" || val == "OFF");
    }
    perf_logger->setLevel(perf_log_enabled ? LogLevel::INFO : LogLevel::FATAL);
    if(perf_log_enabled) {
        perf_logger->addAppender(LogAppender::Type::STDOUT);
    }
    
    std::signal(SIGINT, [](int){g_running = false;});
    std::signal(SIGTERM, [](int){g_running = false;});

}

void Application::run() {
    init();
    DbExecutorMgr::GetInstance();

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
    m_acceptIom = std::make_unique<IOManager>(1, false, "accept");
    m_iom = std::make_unique<IOManager>(worker_threads, false, "http");
    m_iom->schedule([this]() {
        m_chunkManager = std::make_unique<ChunkManager>();
        auto addr = Address::LookupAny("0.0.0.0:8080");
        if (!addr) {
            FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "LookupAny address fail";
            return;
        }
        m_server = std::make_shared<http::HttpServer>(
            true,
            IOManager::GetThis(),
            IOManager::GetThis(),
            m_acceptIom.get());
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
    if(m_iom) {
        m_iom->stop();
    }
    if(m_acceptIom) {
        m_acceptIom->stop();
    }
}

void Application::signalHandler(int) {
    g_running = false;
}

} // namespace FiberServer
