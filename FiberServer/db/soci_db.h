#pragma once

#ifdef FIBERSERVER_USE_SOCI

#include "FiberServer/base/mutex.h"
#include "FiberServer/base/singleton.h"

#include <soci/soci.h>

#include <map>
#include <memory>
#include <string>

namespace FiberServer {

class SociDB : public std::enable_shared_from_this<SociDB> {
public:
    typedef std::shared_ptr<SociDB> ptr;

    explicit SociDB(const std::map<std::string, std::string>& args);

    bool connect();
    soci::session& session();
    int64_t getLastInsertId();

    const std::string& getErrStr() const { return m_errstr; }

private:
    std::string buildConnectionString() const;

private:
    std::map<std::string, std::string> m_params;
    std::unique_ptr<soci::session> m_session;
    std::string m_errstr;
};

class SociManager {
public:
    typedef FiberServer::Mutex MutexType;

    SociDB::ptr get(const std::string& name, int64_t timeout_ms = 3000);
    void registerSoci(const std::string& name,
                      const std::map<std::string, std::string>& params);

private:
    std::map<std::string, std::string> getDbArgs(const std::string& name);

private:
    MutexType m_mutex;
    std::map<std::string, std::map<std::string, std::string> > m_dbDefines;
};

typedef Singleton<SociManager> SociMgr;

}

#endif
