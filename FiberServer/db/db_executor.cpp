#include "db_executor.h"

#include "FiberServer/base/config.h"

#include <cstdlib>

namespace FiberServer {

// 数据库执行器线程数。默认 20，可被 FIBER_DB_WORKER_THREADS 覆盖。
static ConfigVar<uint64_t>::ptr g_dbWorkerThreads = Config::Lookup<uint64_t>(
    "db.worker_threads", 20, "database worker threads");

DbExecutor::DbExecutor() {
    uint64_t worker_threads = g_dbWorkerThreads->getValue();
    // 环境变量优先，方便 Docker/压测环境不改配置文件就调整 DB 工作线程数。
    const char* worker_threads_env = std::getenv("FIBER_DB_WORKER_THREADS");
    if (worker_threads_env && *worker_threads_env) {
        char* end = nullptr;
        uint64_t env_value = std::strtoull(worker_threads_env, &end, 10);
        if (end && *end == '\0' && env_value > 0) {
            worker_threads = env_value;
        }
    }
    if (worker_threads == 0) {
        worker_threads = 1;
    }
    // use_caller=false：db 执行器只使用自己创建的工作线程，不占用当前线程。
    m_iom = std::make_unique<IOManager>(worker_threads, false, "db");
}

DbExecutor::~DbExecutor() {
    if (m_iom) {
        // 停止 db IOManager，等待已投递任务按调度器语义收尾。
        m_iom->stop();
    }
}

}
