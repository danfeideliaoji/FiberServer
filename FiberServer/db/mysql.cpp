#include "mysql.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/config.h"
#include "FiberServer/base/mutex.h"
#include "FiberServer/iomanager.h"
#include "FiberServer/base/macro.h"
namespace FiberServer {

static Logger::ptr g_logger = FIBER_LOG_NAME("system");

// 配置项：mysql.dbs -> map<name, map<key, value>>
static ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_mysql_dbs
    = Config::Lookup("mysql.dbs", std::map<std::string, std::map<std::string, std::string> >(), "mysql dbs");

// ============================================================================
// 1. 时间转换基础工具 (Time Utils)
// ============================================================================

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts) {
    struct tm tm;
    ts = 0;
    localtime_r(&ts, &tm);
    tm.tm_year = mt.year - 1900;
    tm.tm_mon = mt.month - 1;
    tm.tm_mday = mt.day;
    tm.tm_hour = mt.hour;
    tm.tm_min = mt.minute;
    tm.tm_sec = mt.second;
    ts = mktime(&tm);
    if(ts < 0) {
        ts = 0;
    }
    return true;
}

bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt) {
    struct tm tm;
    localtime_r(&ts, &tm);
    mt.year = tm.tm_year + 1900;
    mt.month = tm.tm_mon + 1;
    mt.day = tm.tm_mday;
    mt.hour = tm.tm_hour;
    mt.minute = tm.tm_min;
    mt.second = tm.tm_sec;
    return true;
}



// --- 普通查询结果集 (MySQLRes) ---
MySQLRes::MySQLRes(MYSQL_RES* res, int eno, const char* estr)
    :m_errno(eno)
    ,m_errstr(estr)
    ,m_cur(nullptr)
    ,m_curLength(nullptr) {
    if(res) {
        m_data.reset(res, mysql_free_result);
    }
}

bool MySQLRes::next() {
    m_cur = mysql_fetch_row(m_data.get());
    m_curLength = mysql_fetch_lengths(m_data.get());
    return m_cur;
}

bool MySQLRes::foreach(data_cb cb) {//将结果的每一行都给回调函数处理
    MYSQL_ROW row;
    uint64_t fields = getColumnCount();
    int i = 0;
    while((row = mysql_fetch_row(m_data.get()))) {
        if(!cb(row, fields, i++)) {
            break;
        }
    }
    return true;
}

int MySQLRes::getDataCount() { return mysql_num_rows(m_data.get()); }
int MySQLRes::getColumnCount() { return mysql_num_fields(m_data.get()); }
int MySQLRes::getColumnBytes(int idx) { return m_curLength[idx]; }
int MySQLRes::getColumnType(int idx) { return 0; }
std::string MySQLRes::getColumnName(int idx) { return ""; }
bool MySQLRes::isNull(int idx) { return m_cur[idx] == nullptr; }

int8_t MySQLRes::getInt8(int idx) { return getInt64(idx); }
uint8_t MySQLRes::getUint8(int idx) { return getInt64(idx); }
int16_t MySQLRes::getInt16(int idx) { return getInt64(idx); }
uint16_t MySQLRes::getUint16(int idx) { return getInt64(idx); }
int32_t MySQLRes::getInt32(int idx) { return getInt64(idx); }
uint32_t MySQLRes::getUint32(int idx) { return getInt64(idx); }
int64_t MySQLRes::getInt64(int idx) { return FiberServer::TypeUtil::Atoi(m_cur[idx]); }
uint64_t MySQLRes::getUint64(int idx) { return getInt64(idx); }
float MySQLRes::getFloat(int idx) { return getDouble(idx); }
double MySQLRes::getDouble(int idx) { return FiberServer::TypeUtil::Atof(m_cur[idx]); }
std::string MySQLRes::getString(int idx) { return std::string(m_cur[idx], m_curLength[idx]); }
std::string MySQLRes::getBlob(int idx) { return std::string(m_cur[idx], m_curLength[idx]); }
time_t MySQLRes::getTime(int idx) {
    if(!m_cur[idx]) return 0;
    return FiberServer::Str2Time(m_cur[idx]);
}

// --- 预处理语句结果集 (MySQLStmtRes) ---
MySQLStmtRes::Data::Data() :is_null(0), error(0), type(), length(0), data_length(0), data(nullptr) {}
MySQLStmtRes::Data::~Data() { if(data) delete[] data; }

void MySQLStmtRes::Data::alloc(size_t size) {
    if(data) delete[] data;
    data = new char[size]();
    length = size;
    data_length = size;
}

MySQLStmtRes::ptr MySQLStmtRes::Create(std::shared_ptr<MySQLStmt> stmt) {
    int eno = mysql_stmt_errno(stmt->getRaw());//获得裸指针
    const char* errstr = mysql_stmt_error(stmt->getRaw());
    MySQLStmtRes::ptr rt(new MySQLStmtRes(stmt, eno, errstr));
    if(eno) return rt;

    MYSQL_RES* res = mysql_stmt_result_metadata(stmt->getRaw());
    if(!res) return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));

    int num = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    rt->m_binds.resize(num);
    memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * num);
    rt->m_datas.resize(num);

    for(int i = 0; i < num; ++i) {
        rt->m_datas[i].type = fields[i].type;
        switch(fields[i].type) {
#define XX(m, t) case m: rt->m_datas[i].alloc(sizeof(t)); break;
            XX(MYSQL_TYPE_TINY, int8_t);
            XX(MYSQL_TYPE_SHORT, int16_t);
            XX(MYSQL_TYPE_LONG, int32_t);
            XX(MYSQL_TYPE_LONGLONG, int64_t);
            XX(MYSQL_TYPE_FLOAT, float);
            XX(MYSQL_TYPE_DOUBLE, double);
            XX(MYSQL_TYPE_TIMESTAMP, MYSQL_TIME);
            XX(MYSQL_TYPE_DATETIME, MYSQL_TIME);
            XX(MYSQL_TYPE_DATE, MYSQL_TIME);
            XX(MYSQL_TYPE_TIME, MYSQL_TIME);
#undef XX
            default: rt->m_datas[i].alloc(fields[i].length); break;
        }
        rt->m_binds[i].buffer_type = rt->m_datas[i].type;
        rt->m_binds[i].buffer = rt->m_datas[i].data;
        rt->m_binds[i].buffer_length = rt->m_datas[i].data_length;
        rt->m_binds[i].length = &rt->m_datas[i].length;
        rt->m_binds[i].is_null = &rt->m_datas[i].is_null;
        rt->m_binds[i].error = &rt->m_datas[i].error;
    }

    if(mysql_stmt_bind_result(stmt->getRaw(), &rt->m_binds[0])) {
        return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
    }
    stmt->execute();
    if(mysql_stmt_store_result(stmt->getRaw())) {//将结果存储在客户端缓冲池
        return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
    }
    return rt;
}

MySQLStmtRes::MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno, const std::string& estr)
    :m_errno(eno), m_errstr(estr), m_stmt(stmt) {}

MySQLStmtRes::~MySQLStmtRes() { if(!m_errno) mysql_stmt_free_result(m_stmt->getRaw()); }

bool MySQLStmtRes::next() { return !mysql_stmt_fetch(m_stmt->getRaw()); }
int MySQLStmtRes::getDataCount() { return mysql_stmt_num_rows(m_stmt->getRaw()); }
int MySQLStmtRes::getColumnCount() { return mysql_stmt_field_count(m_stmt->getRaw()); }
int MySQLStmtRes::getColumnBytes(int idx) { return m_datas[idx].length; }
int MySQLStmtRes::getColumnType(int idx) { return m_datas[idx].type; }
std::string MySQLStmtRes::getColumnName(int idx) { return ""; }
bool MySQLStmtRes::isNull(int idx) { return m_datas[idx].is_null; }

#define XX(type) return *(type*)m_datas[idx].data
int8_t MySQLStmtRes::getInt8(int idx) { XX(int8_t); }
uint8_t MySQLStmtRes::getUint8(int idx) { XX(uint8_t); }
int16_t MySQLStmtRes::getInt16(int idx) { XX(int16_t); }
uint16_t MySQLStmtRes::getUint16(int idx) { XX(uint16_t); }
int32_t MySQLStmtRes::getInt32(int idx) { XX(int32_t); }
uint32_t MySQLStmtRes::getUint32(int idx) { XX(uint32_t); }
int64_t MySQLStmtRes::getInt64(int idx) { XX(int64_t); }
uint64_t MySQLStmtRes::getUint64(int idx) { XX(uint64_t); }
float MySQLStmtRes::getFloat(int idx) { XX(float); }
double MySQLStmtRes::getDouble(int idx) { XX(double); }
#undef XX

std::string MySQLStmtRes::getString(int idx) { return std::string(m_datas[idx].data, m_datas[idx].length); }
std::string MySQLStmtRes::getBlob(int idx) { return std::string(m_datas[idx].data, m_datas[idx].length); }
time_t MySQLStmtRes::getTime(int idx) {
    MYSQL_TIME* v = (MYSQL_TIME*)m_datas[idx].data;
    time_t ts = 0;
    mysql_time_to_time_t(*v, ts);
    return ts;
}

// ============================================================================
// 4. SQL 预处理对象 (MySQLStmt)
// ============================================================================

MySQLStmt::ptr MySQLStmt::Create(MySQL::ptr db, const std::string& stmt) {
    auto st = mysql_stmt_init(db->getRaw().get());
    if(!st) return nullptr;
    if(mysql_stmt_prepare(st, stmt.c_str(), stmt.size())) {
        FIBER_LOG_ERROR(g_logger) << "stmt=" << stmt << " errno=" << mysql_stmt_errno(st) << " errstr=" << mysql_stmt_error(st);
        mysql_stmt_close(st);
        return nullptr;
    }
    int count = mysql_stmt_param_count(st);//多少个?
    MySQLStmt::ptr rt(new MySQLStmt(db, st));
    rt->m_binds.resize(count);
    memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * count);
    return rt;
}

MySQLStmt::MySQLStmt(MySQL::ptr db, MYSQL_STMT* stmt) :m_mysql(db), m_stmt(stmt) {}

MySQLStmt::~MySQLStmt() {
    FIBER_ASSERT(Fiber::GetThis()->getState()==Fiber::State::EXEC);
    if(m_stmt) mysql_stmt_close(m_stmt);
    for(auto& i : m_binds) { if(i.buffer) free(i.buffer); }
    FIBER_ASSERT(Fiber::GetThis()->getState()==Fiber::State::EXEC);
}

int MySQLStmt::bind(int idx) {
     idx -= 1;
     m_binds[idx].buffer_type = MYSQL_TYPE_NULL;
     return 0;
    }

#define BIND_COPY(ptr, size) \
    if(m_binds[idx].buffer == nullptr) { m_binds[idx].buffer = malloc(size); } \
    memcpy(m_binds[idx].buffer, ptr, size);

#define BIND_COPY_LEN(ptr, size) \
    if(m_binds[idx].buffer == nullptr) { m_binds[idx].buffer = malloc(size); \
    } else if((size_t)m_binds[idx].buffer_length < (size_t)size) { \
        free(m_binds[idx].buffer); m_binds[idx].buffer = malloc(size); \
    } \
    memcpy(m_binds[idx].buffer, ptr, size); \
    m_binds[idx].buffer_length = size;

int MySQLStmt::bindInt8(int idx, const int8_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_TINY; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = false; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindUint8(int idx, const uint8_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_TINY; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = true; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindInt16(int idx, const int16_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_SHORT; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = false; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindUint16(int idx, const uint16_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_SHORT; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = true; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindInt32(int idx, const int32_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_LONG; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = false; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindUint32(int idx, const uint32_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_LONG; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = true; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindInt64(int idx, const int64_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_LONGLONG; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = false; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindUint64(int idx, const uint64_t& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_LONGLONG; BIND_COPY(&value, sizeof(value)); m_binds[idx].is_unsigned = true; m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindFloat(int idx, const float& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_FLOAT; BIND_COPY(&value, sizeof(value)); m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindDouble(int idx, const double& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_DOUBLE; BIND_COPY(&value, sizeof(value)); m_binds[idx].buffer_length = sizeof(value); return 0; }
int MySQLStmt::bindString(int idx, const char* value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_STRING; BIND_COPY_LEN(value, strlen(value)); return 0; }
int MySQLStmt::bindString(int idx, const std::string& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_STRING; BIND_COPY_LEN(value.c_str(), value.size()); return 0; }
int MySQLStmt::bindBlob(int idx, const void* value, int64_t size) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_BLOB; BIND_COPY_LEN(value, size); return 0; }
int MySQLStmt::bindBlob(int idx, const std::string& value) { idx -= 1; m_binds[idx].buffer_type = MYSQL_TYPE_BLOB; BIND_COPY_LEN(value.c_str(), value.size()); return 0; }
int MySQLStmt::bindTime(int idx, const time_t& value) { return bindString(idx,FiberServer::Time2Str(value)); }
int MySQLStmt::bindNull(int idx) {
    return bind(idx);
}

int MySQLStmt::execute() {
    mysql_stmt_bind_param(m_stmt, &m_binds[0]);
    return mysql_stmt_execute(m_stmt);
}

ISQLData::ptr MySQLStmt::query() {
    mysql_stmt_bind_param(m_stmt, &m_binds[0]);
    return MySQLStmtRes::Create(shared_from_this());
}

int64_t MySQLStmt::getLastInsertId() { return mysql_stmt_insert_id(m_stmt); }
int MySQLStmt::getErrno() { return mysql_stmt_errno(m_stmt); }
std::string MySQLStmt::getErrStr() { const char* e = mysql_stmt_error(m_stmt); return e ? e : ""; }

// ============================================================================
// 5. 核心连接对象 (MySQL Class)
// ============================================================================

//建立一个连接
static MYSQL* mysql_init(std::map<std::string, std::string>& params, const int& timeout) {
    
    MYSQL* mysql = ::mysql_init(nullptr);
    if(mysql == nullptr) {
         FIBER_LOG_ERROR(g_logger) << "mysql_init error"; return nullptr;
        }

    if(timeout > 0) mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    bool close = false;
    // mysql_options(mysql, MYSQL_OPT_RECONNECT, &close);
    unsigned int ssl_mode = SSL_MODE_DISABLED;
    mysql_options(mysql, MYSQL_OPT_SSL_MODE, &ssl_mode);
    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    int port = FiberServer::GetParamValue(params, "port", 0);
    std::string host = GetParamValue<std::string>(params, "host");
    std::string user = GetParamValue<std::string>(params, "user");
    std::string passwd = GetParamValue<std::string>(params, "passwd");
    std::string dbname = GetParamValue<std::string>(params, "dbname");

    if(mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str(), dbname.c_str(), port, NULL, 0) == nullptr) {
        FIBER_LOG_ERROR(g_logger) << "mysql_real_connect(" << host << ", " << port << ", " << dbname << ") error: " << mysql_error(mysql);
        mysql_close(mysql);
        return nullptr;
    }
    return mysql;
}

static MYSQL_RES* my_mysql_query(MYSQL* mysql, const char* sql) {//数据库查询
    if(mysql == nullptr || sql == nullptr) return nullptr;
    if(::mysql_query(mysql, sql)) {
        FIBER_LOG_ERROR(g_logger) << "mysql_query(" << sql << ") error:" << mysql_error(mysql);
        return nullptr;
    }
    MYSQL_RES* res = mysql_store_result(mysql);
    if(res == nullptr) FIBER_LOG_ERROR(g_logger) << "mysql_store_result() error:" << mysql_error(mysql);
    return res;
}

MySQL::MySQL(const std::map<std::string, std::string>& args) :
m_params(args),
 m_lastUsedTime(0)
 , m_hasError(false),
  m_poolSize(20) //表示连接一个数据库的最多连接数
  {}

bool MySQL::connect() {
    // 先关闭旧连接，避免野指针问题
    if(m_mysql) {
        m_mysql.reset();
    }
    MYSQL* m = mysql_init(m_params, 0);
    if(!m) { m_hasError = true; return false; }
    m_hasError = false;
    m_poolSize = GetParamValue(m_params, "pool", 5);
    m_mysql.reset(m, mysql_close);
    return true;
}

bool MySQL::ping() {
    if(!m_mysql) return false;
    if(mysql_ping(m_mysql.get())) 
        { m_hasError = true; return false; 
    }
    m_hasError = false;
    return true;
}
int MySQL::execute(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int MySQL::execute(const char* format, va_list ap) {
    m_cmd = StringUtil::Formatv(format, ap);
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if(r) {
        FIBER_LOG_ERROR(g_logger) << "cmd=" << cmd()
            << ", error: " << getErrStr();
        m_hasError = true;
    } else {
        m_hasError = false;
    }
    return r;
}

int MySQL::execute(const std::string& sql) {
    m_cmd = sql;
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if(r) { 
        FIBER_LOG_ERROR(g_logger) << "cmd=" << cmd() << ", error: " 
        << getErrStr(); m_hasError = true; 
    } 
    else { m_hasError = false; }
    return r;
}

ISQLData::ptr MySQL::query(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rt = query(format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr MySQL::query(const char* format, va_list ap) {
    m_cmd = StringUtil::Formatv(format, ap);
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) {
        m_hasError = true;
        return nullptr;
    }
    m_hasError = false;
    ISQLData::ptr rt(new MySQLRes(res, mysql_errno(m_mysql.get())
                        ,mysql_error(m_mysql.get())));
    return rt;
}

ISQLData::ptr MySQL::query(const std::string& sql) {
    m_cmd = sql;
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) { m_hasError = true; return nullptr; }
    m_hasError = false;
    return ISQLData::ptr(new MySQLRes(res, mysql_errno(m_mysql.get()), mysql_error(m_mysql.get())));
}
std::shared_ptr<MYSQL> MySQL::getRaw() {
    return m_mysql;
}

std::shared_ptr<MySQL> MySQL::getMySQL() {
    return shared_from_this();
}

uint64_t MySQL::getAffectedRows() {
    if(!m_mysql) return 0;
    return mysql_affected_rows(m_mysql.get());
}
int64_t MySQL::getLastInsertId() {
    return mysql_insert_id(m_mysql.get());
}

bool MySQL::isNeedCheck() {
    if((time(0) - m_lastUsedTime) < 5
            && !m_hasError) {
        return false;
    }
    return true;
}
ITransaction::ptr MySQL::openTransaction(bool auto_commit) 
{ 
    return MySQLTransaction::Create(shared_from_this(), auto_commit);
 }
IStmt::ptr MySQL::prepare(const std::string& sql) { 
    return MySQLStmt::Create(shared_from_this(), sql); 
}
const char* MySQL::cmd() {
    return m_cmd.c_str();
}
bool MySQL::use(const std::string& dbname) {
    if(!m_mysql) {
        return false;
    }
    if(m_dbname == dbname) {
        return true;
    }
    if(mysql_select_db(m_mysql.get(), dbname.c_str()) == 0) {
        m_dbname = dbname;
        m_hasError = false;
        return true;
    } else {
        m_dbname = "";
        m_hasError = true;
        return false;
    }
}

std::string MySQL::getErrStr() {
    if(!m_mysql) {
        return "mysql is null";
    }
    const char* str = mysql_error(m_mysql.get());
    if(str) {
        return str;
    }
    return "";
}
int MySQL::getErrno() {
    if(!m_mysql) {
        return -1;
    }
    return mysql_errno(m_mysql.get());
}

uint64_t MySQL::getInsertId() {
    if(m_mysql) {
        return mysql_insert_id(m_mysql.get());
    }
    return 0;
}


// ============================================================================
// 6. 事务控制 (Transaction)
// ============================================================================
MySQLTransaction::MySQLTransaction(MySQL::ptr mysql, bool auto_commit)
    :m_mysql(mysql)
    ,m_autoCommit(auto_commit)
    ,m_isFinished(false)
    ,m_hasError(false) {
}

MySQLTransaction::ptr MySQLTransaction::Create(MySQL::ptr mysql, bool auto_commit) {
    MySQLTransaction::ptr rt(new MySQLTransaction(mysql, auto_commit));
    if(rt->begin()) return rt;
    return nullptr;
}

MySQLTransaction::~MySQLTransaction() { 
    if(m_autoCommit) 
        commit();
    else rollback(); 
}
int64_t MySQLTransaction::getLastInsertId() {
    return m_mysql->getLastInsertId();
}

bool MySQLTransaction::begin() { 
    return execute("BEGIN") == 0; }
bool MySQLTransaction::commit() {
    if(m_isFinished || m_hasError) return !m_hasError;
    int rt = execute("COMMIT");
    if(rt == 0) m_isFinished = true; else m_hasError = true;
    return rt == 0;
}
bool MySQLTransaction::rollback() {
    if(m_isFinished) return true;
    int rt = execute("ROLLBACK");
    if(rt == 0) m_isFinished = true; else m_hasError = true;
    return rt == 0;
}

int MySQLTransaction::execute(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    return execute(format, ap);
}

int MySQLTransaction::execute(const char* format, va_list ap) {
    if(m_isFinished) {
        FIBER_LOG_ERROR(g_logger) << "transaction is finished, format=" << format;
        return -1;
    }
    int rt = m_mysql->execute(format, ap);
    if(rt) {
        m_hasError = true;
    }
    return rt;
}
int MySQLTransaction::execute(const std::string& sql) {
    if(m_isFinished) { 
        FIBER_LOG_ERROR(g_logger) << "transaction is finished, sql=" << sql; return -1; }
    int rt = m_mysql->execute(sql);
    if(rt) m_hasError = true;
    return rt;
}
std::shared_ptr<MySQL> MySQLTransaction::getMySQL() {
    return m_mysql;
}

// ============================================================================
// 7. 连接池管理与工具入口 (Manager & Util)
// ============================================================================

MySQLManager::MySQLManager() :m_maxConn(20) {
    mysql_library_init(0, nullptr, nullptr);
}

MySQLManager::~MySQLManager() {
    if(m_cleanTimer) {
        m_cleanTimer->cancel();
    }
    for(auto& i : m_pools) {
        MutexType::Lock lock(i.second->mutex);
        for(auto& n : i.second->idle) {
             delete  n;
        }
        i.second->idle.clear();
        i.second->totalCount = 0;
    }
    mysql_library_end();
}

std::map<std::string, std::string> MySQLManager::getDbArgs(const std::string& name) {
    // 优先从配置文件读取
    auto config = g_mysql_dbs->getValue();
    auto sit = config.find(name);
    if(sit != config.end()) return sit->second;
    // 其次从手动注册中读取
    MutexType::Lock lock(m_mutex);
    auto it = m_dbDefines.find(name);
    if(it != m_dbDefines.end()) return it->second;
    return {};
}

MySQL* MySQLManager::createConnection(PoolState* pool, int timeout_ms) {
    auto args = getDbArgs("");  
    return nullptr;
}

MySQL::ptr MySQLManager::get(const std::string& name, int64_t timeout_ms) {
    // 获取或初始化 PoolState
    std::shared_ptr<PoolState> pool;
    {
        MutexType::Lock lock(m_mutex);
        auto& sp = m_pools[name];
        if(!sp) {
            sp = std::make_shared<PoolState>();
            auto args = getDbArgs(name);
            sp->minConn = GetParamValue(args, "min_conn", 10);
            sp->maxConn = GetParamValue(args, "max_conn", 30);
            if(sp->maxConn == 0) sp->maxConn = 20;
            if(sp->minConn > sp->maxConn) sp->minConn = sp->maxConn;
        }
        pool = sp;
        // 首次访问时从配置初始化 minConn/maxConn
    }

    // 自定义 deleter，归还连接到池中
    auto deleter = std::bind(&MySQLManager::freeMySQL, this, name, std::placeholders::_1);

    while(true) {
        MutexType::Lock plock(pool->mutex);

        // 1. 尝试从空闲队列取连接
        if(!pool->idle.empty()) {
            MySQL* rt = pool->idle.front();
            pool->idle.pop_front();
            plock.unlock();

            // 有效性检查
            if(!rt->isNeedCheck()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            if(rt->ping()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            if(rt->connect()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            // 连接失效，销毁并减少计数
            {
                MutexType::Lock plock2(pool->mutex);
                if(pool->totalCount > 0) pool->totalCount--;
            }
            delete rt;
            FIBER_LOG_WARN(g_logger) << "reconnect " << name << " fail, retry";
            continue;
        }

        // 2. 无空闲连接，尝试创建新连接
        if(pool->totalCount < pool->maxConn) {
            pool->totalCount++;
            plock.unlock();

            auto args = getDbArgs(name);
            if(args.empty()) {
                MutexType::Lock plock2(pool->mutex);
                pool->totalCount--;
                FIBER_LOG_ERROR(g_logger) << "MySQLManager::get, no config for " << name;
                return nullptr;
            }

            MySQL* rt = new MySQL(args);
            if(rt->connect()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            delete rt;
            MutexType::Lock plock2(pool->mutex);
            pool->totalCount--;
            FIBER_LOG_ERROR(g_logger) << "MySQLManager::get, connect " << name << " fail";
            return nullptr;
        }

        // 达到上限，协程级等待
        if(timeout_ms == 0) {
            plock.unlock();
            FIBER_LOG_WARN(g_logger) << "MySQLManager::get " << name << " pool exhausted, no wait";
            return nullptr;
        }

        auto node = std::make_shared<WaitNode>();
        node->fiber = Fiber::GetThis();
        node->timeout = false;
        pool->waitQueue.push_back(node);
        plock.unlock();

        // 超时定时器
        Timer::ptr timer;
        IOManager* iom = IOManager::GetThis();
        if(timeout_ms > 0 && iom) {
            std::weak_ptr<WaitNode> weak_node(node);
            timer = iom->addTimer(timeout_ms, [weak_node, iom, pool]() {
                auto n = weak_node.lock();
                if(!n) return;
                {
                    MutexType::Lock plock2(pool->mutex);
                    if(n->triggered.exchange(true)) {
                        return; // 已经被 freeMySQL 抢先唤醒了
                    }
                    n->timeout = true;
                    pool->waitQueue.remove(n);
                }
                iom->schedule(n->fiber);
            });
        }

        // 让出当前协程，等待被唤醒
        Fiber::YieldToHold();

        if(timer) {
            timer->cancel();
        }

        if(node->timeout) {
            // 超时，归还直接传递的连接(如果有)
            if(node->conn) {
                MutexType::Lock plock2(pool->mutex);
                pool->idle.emplace_back(node->conn);
            }
            FIBER_LOG_WARN(g_logger) << "MySQLManager::get " << name << " timeout";
            return nullptr;
        }

        // freeMySQL 直接传递了连接，使用它
        if(node->conn) {
            MySQL* rt = node->conn;
            if(!rt->isNeedCheck()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            if(rt->ping()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            if(rt->connect()) {
                rt->m_lastUsedTime = time(0);
                return MySQL::ptr(rt, deleter);
            }
            // 连接失效
            {
                MutexType::Lock plock2(pool->mutex);
                if(pool->totalCount > 0) pool->totalCount--;
            }
            delete rt;
            continue;
        }

        // 没有连接，重新循环尝试
        continue;
    }
}

void MySQLManager::registerMySQL(const std::string& name, const std::map<std::string, std::string>& params) {
    MutexType::Lock lock(m_mutex);
    m_dbDefines[name] = params;
}

void MySQLManager::startCleanTimer(int idle_sec, uint64_t interval_ms) {
    // IOManager* iom = IOManager::GetThis();
    // if(!iom) return;
    // if(m_cleanTimer) m_cleanTimer->cancel();

    // m_cleanTimer = iom->addTimer(interval_ms, [this, idle_sec]() {
    //     time_t now = time(0);
    //     std::vector<MySQL::ptr> toDel;

    //     MutexType::Lock lock(m_mutex);
    //     for(auto& p : m_pools) {
    //         MutexType::Lock plock(p.second->mutex);
    //         auto& pool = *p.second;
    //         // 只回收超过 minConn 的空闲连接
    //         for(auto it = pool.idle.begin(); it != pool.idle.end(); ) {
    //             if(pool.totalCount > pool.minConn
    //                && (int)(now - (*it)->m_lastUsedTime) >= idle_sec) {
    //                 toDel.push_back(it);
    //                 it = pool.idle.erase(it);
    //                 pool.totalCount--;
    //             } else {
    //                 ++it;
    //             }
    //         }
    //     }
    //     lock.unlock();

    //     for(auto& c : toDel) {
    //         delete c;
    //     }
    // }, true);
}

MySQLTransaction::ptr MySQLManager::openTransaction(const std::string& name, bool auto_commit) {
    auto conn = get(name);
    if(!conn) {
        FIBER_LOG_ERROR(g_logger) << "MySQLManager::openTransaction, get(" << name << ") fail";
        return nullptr;
    }
    return MySQLTransaction::Create(conn, auto_commit);
}

void MySQLManager::freeMySQL(const std::string& name, MySQL* m) {
    if(!m) return;

    std::shared_ptr<PoolState> poolPtr;
    {
        MutexType::Lock lock(m_mutex);
        auto it = m_pools.find(name);
        if(it == m_pools.end()) {
            lock.unlock();
            delete m;
            return;
        }
        poolPtr = it->second;
    }
    PoolState& pool = *poolPtr;

    MutexType::Lock plock(pool.mutex);

    // 连接失效则销毁
    if(!m->m_mysql) {
        if(pool.totalCount > 0) pool.totalCount--;
        plock.unlock();
        delete m;
        return;
    }
    if(pool.totalCount > pool.maxConn) {
        if(pool.totalCount > 0) pool.totalCount--;
        plock.unlock();
        delete m;
        return;
    }
    m->m_lastUsedTime = time(0);

    // 优先直接交给等待中的协程，避免经过idle列表的竞争
    while(!pool.waitQueue.empty()) {
        auto node = pool.waitQueue.front();
        pool.waitQueue.pop_front();
        if(node->triggered.exchange(true)) {
            continue;
        }
        node->conn = m;  // 直接传递连接
        plock.unlock();

        IOManager* iom = IOManager::GetThis();
        if(iom && node->fiber) {
            iom->schedule(node->fiber);
        }
        return;
    }

    // 没有等待者，放回空闲队列
    pool.idle.emplace_back(m);
}

ISQLData::ptr MySQLManager::query(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto res = query(name, format, ap);
    va_end(ap);
    return res;
}

ISQLData::ptr MySQLManager::query(const std::string& name, const char* format, va_list ap) {
    auto conn = get(name);
    if(!conn) {
        FIBER_LOG_ERROR(g_logger) << "MySQLManager::query, get(" << name << ") fail, format=" << format;
        return nullptr;
    }
    return conn->query(format, ap);
}

ISQLData::ptr MySQLManager::query(const std::string& name, const std::string& sql) {
    auto conn = get(name);
    if(!conn) {
        FIBER_LOG_ERROR(g_logger) << "MySQLManager::query, get(" << name << ") fail, sql=" << sql;
        return nullptr;
    }
    return conn->query(sql);
}

int MySQLManager::execute(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = execute(name, format, ap);
    va_end(ap);
    return rt;
}

int MySQLManager::execute(const std::string& name, const char* format, va_list ap) {
    auto conn = get(name);
    if(!conn) {
        FIBER_LOG_ERROR(g_logger) << "MySQLManager::execute, get(" << name << ") fail, format=" << format;
        return -1;
    }
    return conn->execute(format, ap);
}

int MySQLManager::execute(const std::string& name, const std::string& sql) {
    auto conn = get(name);
    if(!conn) {
        FIBER_LOG_ERROR(g_logger) << "MySQLManager::execute, get(" << name << ") fail, sql=" << sql;
        return -1;
    }
    return conn->execute(sql);
}





ISQLData::ptr MySQLUtil::Query(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rpy = Query(name, format, ap);
    va_end(ap);
    return rpy;
}

ISQLData::ptr MySQLUtil::Query(const std::string& name, const char* format,va_list ap) {
    auto m = MySQLMgr::GetInstance()->get(name);
    if(!m) {
        return nullptr;
    }
    return m->query(format, ap);
}

ISQLData::ptr MySQLUtil::Query(const std::string& name, const std::string& sql) {
    auto m = MySQLMgr::GetInstance()->get(name);
    if(!m) {
        return nullptr;
    }
    return m->query(sql);
}

ISQLData::ptr MySQLUtil::TryQuery(const std::string& name, uint32_t count, const char* format, ...) {
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, format);
        auto rpy = Query(name, format, ap);
        va_end(ap);
        if(rpy) {
            return rpy;
        }
    }
    return nullptr;
}

ISQLData::ptr MySQLUtil::TryQuery(const std::string& name, uint32_t count, const std::string& sql) {
    for(uint32_t i = 0; i < count; ++i) {
        auto rpy = Query(name, sql);
        if(rpy) {
            return rpy;
        }
    }
    return nullptr;

}

int MySQLUtil::Execute(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rpy = Execute(name, format, ap);
    va_end(ap);
    return rpy;
}

int MySQLUtil::Execute(const std::string& name, const char* format, va_list ap) {
    auto m = MySQLMgr::GetInstance()->get(name);
    if(!m) {
        return -1;
    }
    return m->execute(format, ap);
}

int MySQLUtil::Execute(const std::string& name, const std::string& sql) {
    auto m = MySQLMgr::GetInstance()->get(name);
    if(!m) {
        return -1;
    }
    return m->execute(sql);

}

int MySQLUtil::TryExecute(const std::string& name, uint32_t count, const char* format, ...) {
    int rpy = 0;
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, format);
        rpy = Execute(name, format, ap);
        va_end(ap);
        if(!rpy) {
            return rpy;
        }
    }
    return rpy;
}

int MySQLUtil::TryExecute(const std::string& name, uint32_t count, const std::string& sql) {
    int rpy = 0;
    for(uint32_t i = 0; i < count; ++i) {
        rpy = Execute(name, sql);
        if(!rpy) {
            return rpy;
        }
    }
    return rpy;
}

} 