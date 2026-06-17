#pragma once

#include "FiberServer/base/mutex.h"
#include "FiberServer/base/singleton.h"

#include <soci/soci.h>

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace FiberServer {

class Fiber;
class Scheduler;

// 单个 MySQL 连接的轻量封装。
// SociManager 负责创建、复用和回收 SociDB；业务代码通过 session() 直接使用 SOCI。
class SociDB : public std::enable_shared_from_this<SociDB> {
public:
    typedef std::shared_ptr<SociDB> ptr;

    explicit SociDB(const std::map<std::string, std::string>& args);

    // 根据配置参数建立 SOCI MySQL session。
    bool connect();
    // 暴露原生 SOCI session，方便业务层使用 soci::use/into/transaction。
    soci::session& session();
    // MySQL 自增主键，依赖当前连接上的 LAST_INSERT_ID()。
    int64_t getLastInsertId();

    const std::string& getErrStr() const { return m_errstr; }

private:
    // 把配置项拼成 SOCI MySQL 后端需要的连接字符串。
    std::string buildConnectionString() const;

private:
    // host/port/user/passwd/dbname/max_conn 等配置参数。
    std::map<std::string, std::string> m_params;
    // 一条真实的 SOCI 数据库连接。
    std::unique_ptr<soci::session> m_session;
    // 最近一次连接失败信息。
    std::string m_errstr;
};

// 数据库连接池管理器。
// 每个 name 对应一个 PoolState；get() 返回带自定义 deleter 的 shared_ptr，
// shared_ptr 释放时连接会回到连接池，而不是直接 delete。
class SociManager {
public:
    typedef FiberServer::Mutex MutexType;

    ~SociManager();

    // 获取指定连接池的一条连接。
    // 池满时只支持协程级等待：timeout_ms < 0 表示一直等；
    // timeout_ms == 0 表示不等待；> 0 表示最多等待指定毫秒。
    // 如果调用方不在协程调度器里，池满时直接返回 nullptr。
    SociDB::ptr get(const std::string& name, int64_t timeout_ms = 3000);
    // 代码方式注册数据库配置；优先级低于配置中心 mysql.dbs。
    void registerSoci(const std::string& name,
                      const std::map<std::string, std::string>& params);

private:
    // 等待连接的协程状态。
    // 当连接池没有空闲连接且已达到 maxConn 时，协程会把自己挂到
    // PoolState::waiters 里，然后 YieldToHold() 让出执行权。
    // freeSoci() 或超时定时器会重新 schedule 这个 fiber。
    struct Waiter {
        // 等待者所属的调度器，用来在连接可用或超时时唤醒 fiber。
        Scheduler* scheduler = nullptr;
        // 正在等待数据库连接的协程。
        std::shared_ptr<Fiber> fiber;
        // freeSoci() 分配给等待者的连接；超时时保持为空。
        SociDB* db = nullptr;
        // 是否仍挂在 PoolState::waiters 链表中，防止重复 erase。
        bool linked = false;
        // 等待是否已经结束，可能是拿到连接，也可能是超时。
        bool done = false;
        // true 表示等待由超时定时器结束。
        bool timedOut = false;
        // 自己在 waiters 链表里的位置，超时时可以 O(1) 摘除。
        std::list<std::shared_ptr<Waiter>>::iterator iter;
    };

    // 单个数据库配置对应的连接池状态。
    // m_pools[name] 会对应一个 PoolState，例如 user_info/file_info 各一套。
    struct PoolState {
        // 保护 idle、waiters、totalCount 等池内状态。
        std::mutex mutex;
        // 当前空闲可复用的连接。
        std::list<SociDB*> idle;
        // 正在等待连接的协程队列。
        std::list<std::shared_ptr<Waiter>> waiters;
        // 当前连接总数，包含正在使用的连接和 idle 里的连接。
        uint32_t totalCount = 0;
        // 最大连接数，配置缺省为 30。
        uint32_t maxConn = 30;
    };

    // 连接归还入口，作为 SociDB::ptr 的自定义 deleter 使用。
    void freeSoci(const std::string& name, SociDB* db);
    // 创建并连接一条新数据库连接；失败返回 nullptr。
    SociDB* createConnection(const std::map<std::string, std::string>& args);
    // 从配置中心或 registerSoci() 的手动注册表读取连接参数。
    std::map<std::string, std::string> getDbArgs(const std::string& name);
    // 获取或懒创建某个 name 对应的连接池状态。
    std::shared_ptr<PoolState> getPool(const std::string& name,
                                       const std::map<std::string, std::string>& args);

private:
    // 只保护 m_pools/m_dbDefines 这两个全局映射；单个连接池内部用 PoolState::mutex。
    MutexType m_mutex;

    // name -> 连接池状态。
    std::map<std::string, std::shared_ptr<PoolState> > m_pools;

    // registerSoci() 注册的备用配置。
    std::map<std::string, std::map<std::string, std::string> > m_dbDefines;
};

typedef Singleton<SociManager> SociMgr;

}
