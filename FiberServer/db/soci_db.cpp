#include "soci_db.h"

#ifdef FIBERSERVER_USE_SOCI

#include "FiberServer/base/config.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"

#include <soci/mysql/soci-mysql.h>

#include <sstream>

namespace FiberServer {

static Logger::ptr g_logger = FIBER_LOG_NAME("system");

static ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_soci_dbs
    = Config::Lookup("mysql.dbs", std::map<std::string, std::map<std::string, std::string> >(), "soci mysql dbs");

SociDB::SociDB(const std::map<std::string, std::string>& args)
    : m_params(args) {
}

bool SociDB::connect() {
    try {
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

SociDB::ptr SociManager::get(const std::string& name, int64_t timeout_ms) {
    (void)timeout_ms;

    auto args = getDbArgs(name);
    if (args.empty()) {
        FIBER_LOG_ERROR(g_logger) << "SociManager::get, no config for " << name;
        return nullptr;
    }

    SociDB::ptr db(new SociDB(args));
    if (!db->connect()) {
        return nullptr;
    }
    return db;
}

void SociManager::registerSoci(const std::string& name,
                               const std::map<std::string, std::string>& params) {
    MutexType::Lock lock(m_mutex);
    m_dbDefines[name] = params;
}

std::map<std::string, std::string> SociManager::getDbArgs(const std::string& name) {
    auto config = g_soci_dbs->getValue();
    auto sit = config.find(name);
    if (sit != config.end()) {
        return sit->second;
    }

    MutexType::Lock lock(m_mutex);
    auto it = m_dbDefines.find(name);
    if (it != m_dbDefines.end()) {
        return it->second;
    }
    return {};
}

}

#endif
