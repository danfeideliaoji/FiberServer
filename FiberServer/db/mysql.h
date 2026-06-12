#pragma once
#include <mysql/mysql.h>
#include <memory>
#include <list>
#include <functional>
#include <map>
#include <vector>

#include "db.h"
#include "FiberServer/base/mutex.h"
#include "FiberServer/base/singleton.h"
#include "FiberServer/fiber.h"
#include "FiberServer/iomanager.h"

namespace FiberServer {

class MySQL;
class MySQLStmt;

struct MySQLTime {
    MySQLTime(time_t t) :ts(t) { }
    time_t ts;
};

struct MySQLThreadIniter {
    MySQLThreadIniter() { mysql_thread_init(); }
    ~MySQLThreadIniter() { mysql_thread_end(); }
};

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts);
bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt);

class MySQLRes : public ISQLData {
public:
    typedef std::shared_ptr<MySQLRes> ptr;
    typedef std::function<bool(MYSQL_ROW row, int field_count, int row_no)> data_cb;
    MySQLRes(MYSQL_RES* res, int eno, const char* estr);

    MYSQL_RES* get() const { return m_data.get(); }
    int getErrno() const override { return m_errno; }
    const std::string& getErrStr() const override { return m_errstr; }

    bool foreach(data_cb cb);

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;
    bool next() override;
private:
    int m_errno;
    std::string m_errstr;
    MYSQL_ROW m_cur;
    unsigned long* m_curLength;
    std::shared_ptr<MYSQL_RES> m_data;
};

class MySQLStmtRes : public ISQLData {
friend class MySQLStmt;
public:
    typedef std::shared_ptr<MySQLStmtRes> ptr;
    static MySQLStmtRes::ptr Create(std::shared_ptr<MySQLStmt> stmt);
    ~MySQLStmtRes();

    int getErrno() const override { return m_errno; }
    const std::string& getErrStr() const override { return m_errstr; }

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;
    bool next() override;
private:
    MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno, const std::string& estr);
    struct Data {
        Data();
        ~Data();
        void alloc(size_t size);
        bool is_null;
        bool error;
        enum_field_types type;
        unsigned long length;
        int32_t data_length;
        char* data;
    };
private:
    int m_errno;
    std::string m_errstr;
    std::shared_ptr<MySQLStmt> m_stmt;
    std::vector<MYSQL_BIND> m_binds;
    std::vector<Data> m_datas;
};

class MySQLManager;
class MySQL : public IDB
              ,public std::enable_shared_from_this<MySQL> {
friend class MySQLManager;
public:
    typedef std::shared_ptr<MySQL> ptr;

    MySQL(const std::map<std::string, std::string>& args);

    bool connect();
    bool ping();

    virtual int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<MySQL> getMySQL();
    std::shared_ptr<MYSQL> getRaw();

    uint64_t getAffectedRows();

    ISQLData::ptr query(const char* format, ...) override;
    ISQLData::ptr query(const char* format, va_list ap);
    ISQLData::ptr query(const std::string& sql) override;

    ITransaction::ptr openTransaction(bool auto_commit) override;
    IStmt::ptr prepare(const std::string& sql) override;

    template<typename... Args>
    int execStmt(const char* stmt, Args&&... args);

    template<class... Args>
    ISQLData::ptr queryStmt(const char* stmt, Args&&... args);

    const char* cmd();
    bool use(const std::string& dbname);
    int getErrno() override;
    std::string getErrStr() override;
    uint64_t getInsertId();
private:
    bool isNeedCheck();
private:
    std::map<std::string, std::string> m_params;
    std::shared_ptr<MYSQL> m_mysql;
    std::string m_cmd;
    std::string m_dbname;
    uint64_t m_lastUsedTime;
    bool m_hasError;
    int32_t m_poolSize;
};

class MySQLTransaction : public ITransaction {
public:
    typedef std::shared_ptr<MySQLTransaction> ptr;
    static MySQLTransaction::ptr Create(MySQL::ptr mysql, bool auto_commit);
    ~MySQLTransaction();

    bool begin() override;
    bool commit() override;
    bool rollback() override;

    virtual int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<MySQL> getMySQL();

    bool isAutoCommit() const { return m_autoCommit; }
    bool isFinished() const { return m_isFinished; }
    bool isError() const { return m_hasError; }
private:
    MySQLTransaction(MySQL::ptr mysql, bool auto_commit);
private:
    MySQL::ptr m_mysql;
    bool m_autoCommit;
    bool m_isFinished;
    bool m_hasError;
};

class MySQLStmt : public IStmt
                  ,public std::enable_shared_from_this<MySQLStmt> {
public:
    typedef std::shared_ptr<MySQLStmt> ptr;
    static MySQLStmt::ptr Create(MySQL::ptr db, const std::string& stmt);

    ~MySQLStmt();
    int bind(int idx, const int8_t& value);
    int bind(int idx, const uint8_t& value);
    int bind(int idx, const int16_t& value);
    int bind(int idx, const uint16_t& value);
    int bind(int idx, const int32_t& value);
    int bind(int idx, const uint32_t& value);
    int bind(int idx, const int64_t& value);
    int bind(int idx, const uint64_t& value);
    int bind(int idx, const float& value);
    int bind(int idx, const double& value);
    int bind(int idx, const std::string& value);
    int bind(int idx, const char* value);
    int bind(int idx, const void* value, int len);
    int bind(int idx);

    int bindInt8(int idx, const int8_t& value) override;
    int bindUint8(int idx, const uint8_t& value) override;
    int bindInt16(int idx, const int16_t& value) override;
    int bindUint16(int idx, const uint16_t& value) override;
    int bindInt32(int idx, const int32_t& value) override;
    int bindUint32(int idx, const uint32_t& value) override;
    int bindInt64(int idx, const int64_t& value) override;
    int bindUint64(int idx, const uint64_t& value) override;
    int bindFloat(int idx, const float& value) override;
    int bindDouble(int idx, const double& value) override;
    int bindString(int idx, const char* value) override;
    int bindString(int idx, const std::string& value) override;
    int bindBlob(int idx, const void* value, int64_t size) override;
    int bindBlob(int idx, const std::string& value) override;
    int bindTime(int idx, const time_t& value) override;
    int bindNull(int idx) override;

    int getErrno() override;
    std::string getErrStr() override;

    int execute() override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query() override;

    MYSQL_STMT* getRaw() const { return m_stmt; }
private:
    MySQLStmt(MySQL::ptr db, MYSQL_STMT* stmt);
private:
    MySQL::ptr m_mysql;
    MYSQL_STMT* m_stmt;
    std::vector<MYSQL_BIND> m_binds;
};

/**
 * @brief 协程级MySQL连接池管理器
 * 
 * 特性:
 * - min_connections / max_connections 严格上限控制
 * - 协程级等待与唤醒 (Fiber::YieldToHold / IOManager::schedule)
 * - RAII自动归还连接 (自定义shared_ptr Deleter)
 * - 连接保活与定期回收 (mysql_ping + TimerManager)
 * - 获取连接超时机制 (快速失败)
 */
class MySQLManager {
public:
    typedef FiberServer::Mutex MutexType;

    MySQLManager();
    ~MySQLManager();

    /**
     * @brief 获取一个连接（协程安全）
     * @param name 连接池名称
     * @param timeout_ms 超时时间(毫秒), 0表示不等待, -1表示无限等待
     * @return MySQL::ptr 连接智能指针，超时返回nullptr
     */
    MySQL::ptr get(const std::string& name, int64_t timeout_ms = 3000);

    void registerMySQL(const std::string& name, const std::map<std::string, std::string>& params);

    /// 启动后台保活定时器
    void startCleanTimer(int idle_sec = 30, uint64_t interval_ms = 10000);

    uint32_t getMaxConn() const { return m_maxConn; }
    void setMaxConn(uint32_t v) { m_maxConn = v; }

    int execute(const std::string& name, const char* format, ...);
    int execute(const std::string& name, const char* format, va_list ap);
    int execute(const std::string& name, const std::string& sql);

    ISQLData::ptr query(const std::string& name, const char* format, ...);
    ISQLData::ptr query(const std::string& name, const char* format, va_list ap);
    ISQLData::ptr query(const std::string& name, const std::string& sql);

    MySQLTransaction::ptr openTransaction(const std::string& name, bool auto_commit);

private:

    /// 协程等待节点
    struct WaitNode {
        Fiber::ptr fiber;
        MySQL* conn = nullptr;               // 直接传递的连接
        std::atomic<bool> timeout{false};    // 是否已超时
        std::atomic<bool> triggered{false};  // 是否已被唤醒
    };

    /// 每个连接池的状态
    struct PoolState {
        MutexType  mutex;
        std::list<MySQL*> idle;           // 空闲连接
        uint32_t totalCount = 0;          // 当前总连接数(活跃+空闲)
        uint32_t minConn = 10;             // 最小连接数
        uint32_t maxConn = 30;            // 最大连接数
        std::list<std::shared_ptr<WaitNode>> waitQueue;  // 等待队列
    };
    /// 归还连接到池中，并唤醒等待协程
    void freeMySQL(const std::string& name, MySQL* m);

    /// 创建新的物理连接
    MySQL* createConnection(PoolState* poolstate,int timeout_ms);

    /// 获取指定池的配置参数
    std::map<std::string, std::string> getDbArgs(const std::string& name);


private:
    uint32_t m_maxConn;
    MutexType m_mutex;
    std::map<std::string, std::shared_ptr<PoolState>> m_pools;
    std::map<std::string, std::map<std::string, std::string>> m_dbDefines;
    Timer::ptr m_cleanTimer;
};

class MySQLUtil {
public:
    static ISQLData::ptr Query(const std::string& name, const char* format, ...);
    static ISQLData::ptr Query(const std::string& name, const char* format, va_list ap);
    static ISQLData::ptr Query(const std::string& name, const std::string& sql);

    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const char* format, ...);
    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const std::string& sql);

    static int Execute(const std::string& name, const char* format, ...);
    static int Execute(const std::string& name, const char* format, va_list ap);
    static int Execute(const std::string& name, const std::string& sql);

    static int TryExecute(const std::string& name, uint32_t count, const char* format, ...);
    static int TryExecute(const std::string& name, uint32_t count, const char* format, va_list ap);
    static int TryExecute(const std::string& name, uint32_t count, const std::string& sql);
};

typedef Singleton<MySQLManager> MySQLMgr;

namespace {

template<size_t N, typename... Args>
struct MySQLBinder {
    static int Bind(std::shared_ptr<MySQLStmt> stmt) { return 0; }
};

template<typename... Args>
int bindX(MySQLStmt::ptr stmt, Args&... args) {
    return MySQLBinder<1, Args...>::Bind(stmt, args...);
}
}

template<typename... Args>
int MySQL::execStmt(const char* stmt, Args&&... args) {
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if(!st) return -1;
    int rt = bindX(st, args...);
    if(rt != 0) return rt;
    return st->execute();
}

template<class... Args>
ISQLData::ptr MySQL::queryStmt(const char* stmt, Args&&... args) {
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if(!st) return nullptr;
    int rt = bindX(st, args...);
    if(rt != 0) return nullptr;
    return st->query();
}

namespace {

template<size_t N, typename Head, typename... Tail>
struct MySQLBinder<N, Head, Tail...> {
    static int Bind(MySQLStmt::ptr stmt, const Head&, Tail&...) {
        static_assert(sizeof...(Tail) < 0, "invalid type");
        return 0;
    }
};

#define XX(type, type2) \
template<size_t N, typename... Tail> \
struct MySQLBinder<N, type, Tail...> { \
    static int Bind(MySQLStmt::ptr stmt \
                    , type2 value \
                    , Tail&... tail) { \
        int rt = stmt->bind(N, value); \
        if(rt != 0) return rt; \
        return MySQLBinder<N + 1, Tail...>::Bind(stmt, tail...); \
    } \
};

XX(char*, char*);
XX(const char*, char*);
XX(std::string, std::string&);
XX(int8_t, int8_t&);
XX(uint8_t, uint8_t&);
XX(int16_t, int16_t&);
XX(uint16_t, uint16_t&);
XX(int32_t, int32_t&);
XX(uint32_t, uint32_t&);
XX(int64_t, int64_t&);
XX(uint64_t, uint64_t&);
XX(float, float&);
XX(double, double&);
#undef XX
}
}
