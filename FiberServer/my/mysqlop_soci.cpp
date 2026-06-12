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

bool ExistsByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        auto& sql = db->session();
        int id = 0;
        sql << "SELECT id FROM file_info WHERE md5 = :md5 LIMIT 1",
               soci::use(md5, "md5"),
               soci::into(id);
        return sql.got_data();
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI ExistsByMd5 error: " << e.what();
        return false;
    }
}

bool ExistsByMd5AndUser(SociDB::ptr db, const std::string& md5, const std::string& user) {
    if (!db) {
        return false;
    }

    try {
        auto& sql = db->session();
        int id = 0;
        sql << "SELECT id FROM file_info WHERE md5 = :md5 AND url = :user LIMIT 1",
               soci::use(md5, "md5"),
               soci::use(user, "user"),
               soci::into(id);
        return sql.got_data();
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI ExistsByMd5AndUser error: " << e.what();
        return false;
    }
}

bool CreateFile(SociDB::ptr db, const std::string& md5, const std::string& file_id,
                const std::string& url, const std::string& filename,
                int64_t size, const std::string& type) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "INSERT INTO file_info (md5, file_id, url, filename, size, type, count) "
                         "VALUES (:md5, :file_id, :url, :filename, :size, :type, 1)",
                         soci::use(md5, "md5"),
                         soci::use(file_id, "file_id"),
                         soci::use(url, "url"),
                         soci::use(filename, "filename"),
                         soci::use(size, "size"),
                         soci::use(type, "type");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI CreateFile error: " << e.what();
        return false;
    }
}

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

bool IncrementCount(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE file_info SET count = count + 1 WHERE md5 = :md5",
                         soci::use(md5, "md5");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI IncrementCount error: " << e.what();
        return false;
    }
}

}

namespace file_shared {

bool ExistsByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        auto& sql = db->session();
        std::string file_md5;
        sql << "SELECT file_md5 FROM file_shared WHERE file_md5 = :md5 LIMIT 1",
               soci::use(md5, "md5"),
               soci::into(file_md5);
        return sql.got_data();
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI shared ExistsByMd5 error: " << e.what();
        return false;
    }
}

std::string GetFileIdByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return "";
    }

    try {
        auto& sql = db->session();
        std::string file_id;
        sql << "SELECT file_id FROM file_shared WHERE file_md5 = :md5",
               soci::use(md5, "md5"),
               soci::into(file_id);
        if (!sql.got_data()) {
            return "";
        }
        return file_id;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileIdByMd5 error: " << e.what();
        return "";
    }
}

bool CreateShared(SociDB::ptr db, const std::string& md5,
                  const std::string& file_id, int64_t file_size) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "INSERT INTO file_shared (file_md5, file_id, file_size, ref_count) "
                         "VALUES (:md5, :file_id, :file_size, 1)",
                         soci::use(md5, "md5"),
                         soci::use(file_id, "file_id"),
                         soci::use(file_size, "file_size");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI CreateShared error: " << e.what();
        return false;
    }
}

bool IncrementRef(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE file_shared SET ref_count = ref_count + 1 WHERE file_md5 = :md5",
                         soci::use(md5, "md5");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI IncrementRef error: " << e.what();
        return false;
    }
}

int DecrementRef(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return -1;
    }

    try {
        auto& sql = db->session();
        sql << "UPDATE file_shared SET ref_count = ref_count - 1 WHERE file_md5 = :md5 AND ref_count > 0",
               soci::use(md5, "md5");
        int ref_count = -1;
        sql << "SELECT ref_count FROM file_shared WHERE file_md5 = :md5",
               soci::use(md5, "md5"),
               soci::into(ref_count);
        if (!sql.got_data()) {
            return -1;
        }
        return ref_count;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DecrementRef error: " << e.what();
        return -1;
    }
}

bool DeleteShared(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM file_shared WHERE file_md5 = :md5",
                         soci::use(md5, "md5");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteShared error: " << e.what();
        return false;
    }
}

}

}

#endif
