#pragma once
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <memory>
#include "FiberServer/base/log.h"
#include "FiberServer/base/config.h"
#include "FiberServer/iomanager.h"
#include "FiberServer/net/address.h"
#include "FiberServer/net/http/http_server.h"
#include "chunkManager.h"
namespace FiberServer {

class Application {
public:
    Application();
    ~Application();
    void run();
     void stop();

private:
    void signalHandler(int);
    void init();

private:
    static std::atomic<bool> g_running;
    std::unique_ptr<IOManager> m_iom;
    std::unique_ptr<ChunkManager> m_chunkManager;
    http::HttpServer::ptr m_server;
    std::thread::id m_mainThreadId;
};

} // namespace FiberServer
