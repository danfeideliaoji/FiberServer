#include "soci_db.h"

#include "FiberServer/base/config.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/iomanager.h"

#include <soci/mysql/soci-mysql.h>

#include <chrono>
#include <sstream>

namespace FiberServer {

static Logger::ptr g_logger = FIBER_LOG_NAME("system");
static Logger::ptr g_perf_logger = FIBER_LOG_NAME("perf");

// 数据库配置入口：
// mysql.dbs.<name> 下放 host/port/user/passwd/dbname/max_conn 等参数。
static ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_soci_dbs
    = Config::Lookup("mysql.dbs", std::map<std::string, std::map<std::string, std::string> >(), "soci mysql dbs");

SociDB::SociDB(const std::map<std::string, std::string>& args)
    : m_params(args) {
}

bool SociDB::connect() {
    try {
        // SOCI session 构造时会立即建立到 MySQL 的连接。
        m_session.reset(new soci::session(soci::mysql, buildConnectionString()));
        m_errstr.clear();
        return true;
    } catch (const std::exception& e) {
        m_errstr = e.what();
        FIBER_LOG_ERROR(g_logger) << "SociDB::connect error: " << m_errstr;
        return false;
    }
}

soci::session& SociDB::session() {
    return *m_session;
}

int64_t SociDB::getLastInsertId() {
    long long id = 0;
    session() << "SELECT LAST_INSERT_ID()", soci::into(id);
    return id;
}

std::string SociDB::buildConnectionString() const {
    // 这里给每个字段都留默认值，便于本地开发只配置必要项。
    std::string host = GetParamValue<std::string>(m_params, "host", "127.0.0.1");
    int port = GetParamValue<int>(m_params, "port", 3306);
    std::string user = GetParamValue<std::string>(m_params, "user", "root");
    std::string passwd = GetParamValue<std::string>(m_params, "passwd", "");
    std::string dbname = GetParamValue<std::string>(m_params, "dbname", "");

    std::ostringstream ss;
    ss << "db=" << dbname
       << " user=" << user
       << " password=" << passwd
       << " host=" << host
       << " port=" << port
       << " charset=utf8mb4";
    return ss.str();
}

SociManager::~SociManager() {
    MutexType::Lock lock(m_mutex);
    for (auto& item : m_pools) {
        auto& pool = item.second;
        std::unique_lock<std::mutex> pool_lock(pool->mutex);
        // 这里只释放 idle 里的连接。正常停机时业务持有的连接应先释放回池。
        for (auto* db : pool->idle) {
            delete db;
        }
        pool->idle.clear();
        pool->totalCount = 0;
    }
}

SociDB::ptr SociManager::get(const std::string& name, int64_t timeout_ms) {
    auto total_start = std::chrono::steady_clock::now();
    auto args = getDbArgs(name);
    if (args.empty()) {
        FIBER_LOG_ERROR(g_logger) << "SociManager::get, no config for " << name;
        return nullptr;
    }

    auto pool = getPool(name, args);
    SociDB* raw = nullptr;
    bool should_create = false;
    std::string source = "idle";
    int64_t pool_wait_ms = 0;
    int64_t create_ms = 0;
    IOManager* iom = IOManager::GetThis();
    Scheduler* scheduler = Scheduler::GetThis();

    {
        std::unique_lock<std::mutex> lock(pool->mutex);
        if (!pool->idle.empty()) {
            // 优先复用空闲连接，这是最便宜的路径。
            raw = pool->idle.front();
            pool->idle.pop_front();
        } else if (pool->totalCount < pool->maxConn) {
            // 连接数未达上限时先占一个名额，真正连接动作放到锁外执行。
            ++pool->totalCount;
            should_create = true;
            source = "create";
        } else if (timeout_ms == 0) {
            FIBER_LOG_WARN(g_logger) << "SociManager::get " << name << " no idle connection";
            return nullptr;
        } else if (iom && scheduler) {
            // 协程环境下不阻塞工作线程：把当前 fiber 挂到等待队列，
            // 等连接归还或定时器超时后再由 scheduler 唤醒。
            auto waiter = std::make_shared<Waiter>();
            waiter->scheduler = scheduler;
            waiter->fiber = Fiber::GetThis();
            waiter->iter = pool->waiters.insert(pool->waiters.end(), waiter);
            waiter->linked = true;
            lock.unlock();

            Timer::ptr timer;
            auto wait_start = std::chrono::steady_clock::now();
            if (timeout_ms > 0) {
                std::weak_ptr<Waiter> weak_waiter(waiter);
                timer = iom->addTimer(timeout_ms, [pool, weak_waiter]() {
                    auto waiter = weak_waiter.lock();
                    if (!waiter) {
                        return;
                    }

                    Scheduler* scheduler = nullptr;
                    Fiber::ptr fiber;
                    {
                        std::unique_lock<std::mutex> lock(pool->mutex);
                        if (waiter->done) {
                            return;
                        }
                        if (waiter->linked) {
                            pool->waiters.erase(waiter->iter);
                            waiter->linked = false;
                        }
                        // 超时和连接归还都可能唤醒同一个 waiter。
                        // done/link 标记保证只有一边真正完成等待。
                        waiter->done = true;
                        waiter->timedOut = true;
                        scheduler = waiter->scheduler;
                        fiber = waiter->fiber;
                    }
                    if (scheduler && fiber) {
                        scheduler->schedule(fiber);
                    }
                });
            }

            Fiber::YieldToHold();
            if (timer) {
                timer->cancel();
            }
            pool_wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - wait_start).count();

            lock.lock();
            if (waiter->db) {
                source = "wait";
                raw = waiter->db;
            } else {
                // 被唤醒但没有拿到连接，说明等待超时或被清理。
                if (waiter->linked) {
                    pool->waiters.erase(waiter->iter);
                    waiter->linked = false;
                }
                waiter->done = true;
                FIBER_LOG_WARN(g_logger) << "SociManager::get " << name
                    << " timeout wait_ms=" << pool_wait_ms;
                return nullptr;
            }
        } else {
            FIBER_LOG_WARN(g_logger) << "SociManager::get " << name
                << " pool exhausted outside fiber scheduler";
            return nullptr;
        }
    }

    if (should_create) {
        // 建立数据库连接可能阻塞，不能拿着 pool->mutex 做。
        auto create_start = std::chrono::steady_clock::now();
        raw = createConnection(args);
        create_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - create_start).count();
        if (!raw) {
            std::unique_lock<std::mutex> lock(pool->mutex);
            // 创建失败要归还前面占用的连接名额。
            --pool->totalCount;
            return nullptr;
        }
    }

    uint32_t pool_total = 0;
    size_t pool_idle = 0;
    {
        std::unique_lock<std::mutex> lock(pool->mutex);
        pool_total = pool->totalCount;
        pool_idle = pool->idle.size();
    }
    int64_t total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - total_start).count();
    // 连接池性能日志用于观察是否频繁创建连接、是否长时间等连接。
    FIBER_LOG_INFO(g_perf_logger) << "perf component=soci_pool"
        << " name=" << name
        << " source=" << source
        << " pool_wait_ms=" << pool_wait_ms
        << " create_ms=" << create_ms
        << " total_ms=" << total_ms
        << " pool_total=" << pool_total
        << " pool_idle=" << pool_idle;

    // 用 shared_ptr 的 deleter 自动归还连接，业务层不需要手动调用 freeSoci()。
    return SociDB::ptr(raw, [this, name](SociDB* db) {
        this->freeSoci(name, db);
    });
}

void SociManager::registerSoci(const std::string& name,
                               const std::map<std::string, std::string>& params) {
    MutexType::Lock lock(m_mutex);
    // 手动注册的配置作为配置中心缺省时的兜底。
    m_dbDefines[name] = params;
}

void SociManager::freeSoci(const std::string& name, SociDB* db) {
    if (!db) {
        return;
    }

    std::shared_ptr<PoolState> pool;
    {
        MutexType::Lock lock(m_mutex);
        auto it = m_pools.find(name);
        if (it != m_pools.end()) {
            pool = it->second;
        }
    }

    if (!pool) {
        delete db;
        return;
    }

    std::shared_ptr<Waiter> waiter;
    {
        std::unique_lock<std::mutex> lock(pool->mutex);
        // 归还连接时优先交给最早等待的协程，而不是先放回 idle。
        // 已经超时的 waiter 会被跳过。
        while (!pool->waiters.empty()) {
            waiter = pool->waiters.front();
            pool->waiters.pop_front();
            waiter->linked = false;
            if (waiter->done) {
                waiter.reset();
                continue;
            }
            waiter->done = true;
            waiter->db = db;
            db = nullptr;
            break;
        }
        if (db) {
            // 没有协程在等，连接回到空闲池等待下次复用。
            pool->idle.push_back(db);
        }
    }
    if (waiter && waiter->scheduler && waiter->fiber) {
        waiter->scheduler->schedule(waiter->fiber);
    }
}

SociDB* SociManager::createConnection(const std::map<std::string, std::string>& args) {
    SociDB* db = new SociDB(args);
    if (!db->connect()) {
        delete db;
        return nullptr;
    }
    return db;
}

std::map<std::string, std::string> SociManager::getDbArgs(const std::string& name) {
    // 优先使用运行时配置，方便通过配置文件统一管理多个库。
    auto config = g_soci_dbs->getValue();
    auto sit = config.find(name);
    if (sit != config.end()) {
        return sit->second;
    }

    MutexType::Lock lock(m_mutex);
    // 如果配置中心没有，再看代码注册的备用配置。
    auto it = m_dbDefines.find(name);
    if (it != m_dbDefines.end()) {
        return it->second;
    }
    return {};
}

std::shared_ptr<SociManager::PoolState> SociManager::getPool(
        const std::string& name,
        const std::map<std::string, std::string>& args) {
    MutexType::Lock lock(m_mutex);
    auto it = m_pools.find(name);
    if (it != m_pools.end()) {
        return it->second;
    }

    std::shared_ptr<PoolState> pool(new PoolState());
    // 兼容 max_conn 和旧的 connection 字段；最终至少保留 1 条连接上限。
    pool->maxConn = GetParamValue<uint32_t>(args, "max_conn",
        GetParamValue<uint32_t>(args, "connection", 30));
    if (pool->maxConn == 0) {
        pool->maxConn = 1;
    }
    m_pools[name] = pool;
    return pool;
}

}
