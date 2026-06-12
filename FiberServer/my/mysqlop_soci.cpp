#include "mysqlop.h"

#ifdef FIBERSERVER_USE_SOCI

#include "FiberServer/base/log.h"

#include <soci/soci.h>

namespace FiberServer {

static Logger::ptr g_logger = FIBER_LOG_NAME("system");

static std::shared_ptr<UserInfo> UserInfoFromRow(const soci::row& row) {
    std::shared_ptr<UserInfo> info(new UserInfo());
    info->id = row.get<long long>("id");
    info->username = row.get<std::string>("username");
    info->password = row.get<std::string>("password");
    info->salt = row.get<std::string>("salt");
    info->nickname = row.get<std::string>("nickname", "");
    info->status = static_cast<int8_t>(row.get<int>("status", 0));
    info->last_login = 0;
    info->create_time = 0;
    info->update_time = 0;
    return info;
}

namespace user_info {

bool CreateUser(SociDB::ptr db, const std::string& username,
                const std::string& password, const std::string& salt,
                const std::string& nickname, int64_t& out_id) {
    if (!db) {
        return false;
    }

    try {
        auto& sql = db->session();
        sql << "INSERT INTO user_info (username, password, salt, nickname, status) "
               "VALUES (:username, :password, :salt, :nickname, 1)",
               soci::use(username, "username"),
               soci::use(password, "password"),
               soci::use(salt, "salt"),
               soci::use(nickname, "nickname");
        out_id = db->getLastInsertId();
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI CreateUser error: " << e.what();
        return false;
    }
}

std::shared_ptr<UserInfo> GetUserByUsername(SociDB::ptr db, const std::string& username) {
    if (!db) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT id, username, password, salt, nickname, status "
               "FROM user_info WHERE username = :username",
               soci::use(username, "username"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return UserInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetUserByUsername error: " << e.what();
        return nullptr;
    }
}

}

}

#endif
