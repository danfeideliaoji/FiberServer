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

static std::shared_ptr<FileInfo> FileInfoFromRow(const soci::row& row) {
    std::shared_ptr<FileInfo> info(new FileInfo());
    info->id = row.get<long long>("id");
    info->md5 = row.get<std::string>("md5");
    info->file_id = row.get<std::string>("file_id");
    info->url = row.get<std::string>("url");
    info->filename = row.get<std::string>("filename", "");
    info->size = row.get<long long>("size");
    info->type = row.get<std::string>("type", "");
    info->count = row.get<int>("count", 0);
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

namespace file_info {

std::shared_ptr<FileInfo> GetFileByUserAndFilename(SociDB::ptr db,
                                                   const std::string& user,
                                                   const std::string& filename) {
    if (!db) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT id, md5, file_id, url, filename, size, type, count, create_time, update_time "
               "FROM file_info WHERE url = :user AND filename = :filename",
               soci::use(user, "user"),
               soci::use(filename, "filename"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return FileInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileByUserAndFilename error: " << e.what();
        return nullptr;
    }
}

std::vector<std::shared_ptr<FileInfo>> GetFileListByUser(SociDB::ptr db,
                                                         const std::string& user,
                                                         int offset,
                                                         int limit) {
    std::vector<std::shared_ptr<FileInfo>> files;
    if (!db) {
        return files;
    }

    try {
        auto& sql = db->session();
        soci::rowset<soci::row> rows =
            (sql.prepare << "SELECT id, md5, file_id, url, filename, size, type, count, create_time, update_time "
                            "FROM file_info WHERE url = :user ORDER BY id DESC LIMIT :limit OFFSET :offset",
                            soci::use(user, "user"),
                            soci::use(limit, "limit"),
                            soci::use(offset, "offset"));
        for (const auto& row : rows) {
            files.push_back(FileInfoFromRow(row));
        }
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileListByUser error: " << e.what();
    }

    return files;
}

bool DeleteFileRecordByUserAndFilename(SociDB::ptr db,
                                       const std::string& user,
                                       const std::string& filename) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM file_info WHERE url = :user AND filename = :filename",
                         soci::use(user, "user"),
                         soci::use(filename, "filename");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteFileRecordByUserAndFilename error: " << e.what();
        return false;
    }
}

}

}

#endif
