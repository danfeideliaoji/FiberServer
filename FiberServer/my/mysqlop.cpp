#include "mysqlop.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/macro.h"
namespace FiberServer{
static Logger::ptr g_logger = FIBER_LOG_NAME("system");
static bool UserInfoFromResult(ISQLData::ptr res, std::shared_ptr<UserInfo>& info) {
    if (!res || res->getDataCount() <= 0) {
        return false;
    }
    info = std::make_shared<UserInfo>();
    info->id = res->getInt64(0);
    info->username = res->getString(1);
    info->password = res->getString(2);
    info->salt = res->getString(3);
    info->nickname = res->getString(4);
    info->status = res->getInt8(5);
    info->last_login = res->getTime(6);
    info->create_time = res->getTime(7);
    info->update_time = res->getTime(8);
    return true;
}

static bool FileInfoFromResult(ISQLData::ptr res, std::shared_ptr<FileInfo>& info) {
    if (!res || res->getDataCount() <= 0) {
        return false;
    }
    //SELECT id, md5, file_id, url,filename,size, type, count, create_time, update_time
    info = std::make_shared<FileInfo>();
    info->id = res->getInt64(0);
    // FIBER_LOG_INFO(g_logger)<<info->id;
    info->md5 = res->getString(1);
    info->file_id = res->getString(2);
    info->url = res->getString(3);
    info->filename = res->getString(4);
    
    info->size = res->getInt64(5);
    info->type = res->getString(6);
    info->count = res->getInt32(7);
    info->create_time = res->getTime(8);
    // info->update_time = res->getTime(9);
    return true;
}

namespace user_info {

bool CreateUser(MySQL::ptr db, const std::string& username,
                const std::string& password, const std::string& salt,
                const std::string& nickname, int64_t& out_id) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "INSERT INTO user_info (username, password, salt, nickname, status) VALUES (?, ?, ?, ?, 1)");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, username);
    stmt->bindString(2, password);
    stmt->bindString(3, salt);
    stmt->bindString(4, nickname);
    if (stmt->execute() != 0) {
        return false;
    }
    out_id = db->getLastInsertId();
    return true;
}

std::shared_ptr<UserInfo> GetUserById(MySQL::ptr db, int64_t id) {
    if (!db) {
        return nullptr;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, username, password, salt, nickname, status, last_login, create_time, update_time FROM user_info WHERE id = ?");
    if (!stmt) {
        return nullptr;
    }
    stmt->bindInt64(1, id);
    auto res = stmt->query();
    if (!res) {
        return nullptr;
    }
    std::shared_ptr<UserInfo> info;
    if (res->next()) {
        UserInfoFromResult(res, info);
    }
    return info;
}

std::shared_ptr<UserInfo> GetUserByUsername(MySQL::ptr db, const std::string& username) {
    if (!db) {
        return nullptr;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, username, password, salt, nickname, status, last_login, create_time, update_time FROM user_info WHERE username = ?");
    if (!stmt) {
        return nullptr;
    }
    stmt->bindString(1, username);
    FIBER_ASSERT2(Fiber::GetThis()->getState()==Fiber::State::EXEC, Fiber::GetThis()->getState())
    auto res = stmt->query();
    FIBER_ASSERT2(Fiber::GetThis()->getState()==Fiber::State::EXEC, Fiber::GetThis()->getState())
    if (!res) {
        return nullptr;
    }
    std::shared_ptr<UserInfo> info;
    if (res->next()) {
        UserInfoFromResult(res, info);
    }
     FIBER_ASSERT2(Fiber::GetThis()->getState()==Fiber::State::EXEC, Fiber::GetThis()->getState())
    return info;
}

bool UpdatePassword(MySQL::ptr db, int64_t user_id,
                    const std::string& password, const std::string& salt) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE user_info SET password = ?, salt = ? WHERE id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, password);
    stmt->bindString(2, salt);
    stmt->bindInt64(3, user_id);
    return stmt->execute() == 0;
}

bool UpdateLastLogin(MySQL::ptr db, int64_t user_id) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE user_info SET last_login = NOW() WHERE id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindInt64(1, user_id);
    return stmt->execute() == 0;
}

bool UpdateNickname(MySQL::ptr db, int64_t user_id, const std::string& nickname) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE user_info SET nickname = ? WHERE id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, nickname);
    stmt->bindInt64(2, user_id);
    return stmt->execute() == 0;
}



bool UpdateStatus(MySQL::ptr db, int64_t user_id, int8_t status) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE user_info SET status = ? WHERE id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindInt8(1, status);
    stmt->bindInt64(2, user_id);
    return stmt->execute() == 0;
}

bool DeleteUser(MySQL::ptr db, int64_t user_id) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "DELETE FROM user_info WHERE id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindInt64(1, user_id);
    return stmt->execute() == 0;
}

std::vector<std::shared_ptr<UserInfo>> GetUsersByStatus(MySQL::ptr db, int8_t status) {
    std::vector<std::shared_ptr<UserInfo>> users;
    if (!db) {
        return users;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, username, password, salt, nickname, status, last_login, create_time, update_time FROM user_info WHERE status = ?");
    if (!stmt) {
        return users;
    }
    stmt->bindInt8(1, status);
    auto res = stmt->query();
    if (!res) {
        return users;
    }
    while (res->next()) {
        std::shared_ptr<UserInfo> info;
        if (UserInfoFromResult(res, info)) {
            users.push_back(info);
        }
    }
    return users;
}

int64_t GetUserCount(MySQL::ptr db) {
    if (!db) {
        return 0;
    }
    auto res = db->query("SELECT COUNT(*) FROM user_info");
    if (!res || res->getDataCount() <= 0) {
        return 0;
    }
    res->next();
    return res->getInt64(0);
}

}

namespace file_info {

bool ExistsByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db, "SELECT id FROM file_info WHERE md5 = ? LIMIT 1");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, md5);
    auto res = stmt->query();
    return res && res->next();
}

bool ExistsByFileId(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db, "SELECT id FROM file_info WHERE file_id = ? LIMIT 1");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, file_id);
    auto res = stmt->query();
    return res && res->next();
}

bool CreateFile(MySQL::ptr db, const std::string& md5, const std::string& file_id,
                const std::string& username, const std::string& filename, int64_t size, const std::string& type) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "INSERT INTO file_info (md5, file_id, url, filename, size, type, count) VALUES (?, ?, ?, ?, ?, ?, 1)");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, md5);
    stmt->bindString(2, file_id);
    stmt->bindString(3, username);
    stmt->bindString(4, filename);
    // FIBER_LOG_INFO(g_logger)<<"create"<< filename;
    stmt->bindInt64(5, size);
    stmt->bindString(6, type);
    return stmt->execute() == 0;
}

bool UpdateFile(MySQL::ptr db, const std::string& file_id,
                const std::string& url, int64_t size, const std::string& type) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE file_info SET url = ?, size = ?, type = ? WHERE file_id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, url);
    stmt->bindInt64(2, size);
    stmt->bindString(3, type);
    stmt->bindString(4, file_id);
    return stmt->execute() == 0;
}

bool DeleteFileRecord(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db, "DELETE FROM file_info WHERE file_id = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, file_id);
    return stmt->execute() == 0;
}

bool DeleteFileRecordByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db, "DELETE FROM file_info WHERE md5 = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, md5);
    return stmt->execute() == 0;
}

int64_t GetTotalStorageSize(MySQL::ptr db) {
    if (!db) {
        return 0;
    }
    auto res = db->query("SELECT COALESCE(SUM(size), 0) FROM file_info");
    if (!res || res->getDataCount() <= 0) {
        return 0;
    }
    res->next();
    return res->getInt64(0);
}

std::shared_ptr<FileInfo> GetFileById(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return nullptr;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, md5, file_id, url, size, type, count, create_time, update_time FROM file_info WHERE file_id = ?");
    if (!stmt) {
        return nullptr;
    }
    stmt->bindString(1, file_id);
    auto res = stmt->query();
    if (!res) {
        return nullptr;
    }
    std::shared_ptr<FileInfo> info;
    if (res->next()) {
        FileInfoFromResult(res, info);
    }
    return info;
}

std::shared_ptr<FileInfo> GetFileByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return nullptr;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, md5, file_id, url, filename, size, type, count, create_time, update_time FROM file_info WHERE md5 = ?");
    if (!stmt) {
        return nullptr;
    }
    stmt->bindString(1, md5);
    auto res = stmt->query();
    if (!res) {
        return nullptr;
    }
    std::shared_ptr<FileInfo> info;
    if (res->next()) {
        FileInfoFromResult(res, info);
    }
    return info;
}

std::shared_ptr<FileInfo> GetFileByUserAndFilename(MySQL::ptr db, const std::string& user, const std::string& filename) {
    if (!db) {
        return nullptr;
    }
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, md5, file_id, url, filename, size, type, count, create_time, update_time FROM file_info WHERE url = ? AND filename = ?");
    if (!stmt) {
        return nullptr;
    }
    stmt->bindString(1, user);
    stmt->bindString(2, filename);
    auto res = stmt->query();
    if (!res) {
        return nullptr;
    }
    std::shared_ptr<FileInfo> info;
    if (res->next()) {
        FileInfoFromResult(res, info);
    }
    return info;
}

int64_t GetFileSize(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return 0;
    }
    auto stmt = MySQLStmt::Create(db, "SELECT size FROM file_info WHERE file_id = ?");
    if (!stmt) {
        return 0;
    }
    stmt->bindString(1, file_id);
    auto res = stmt->query();
    if (!res || !res->next()) {
        return 0;
    }
    return res->getInt64(0);
}

int64_t GetFileSizeByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return 0;
    }
    auto stmt = MySQLStmt::Create(db, "SELECT size FROM file_info WHERE md5 = ?");
    if (!stmt) {
        return 0;
    }
    stmt->bindString(1, md5);
    auto res = stmt->query();
    if (!res || !res->next()) {
        return 0;
    }
    return res->getInt64(0);
}

std::string GetFileUrl(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return "";
    }
    auto stmt = MySQLStmt::Create(db, "SELECT url FROM file_info WHERE file_id = ?");
    if (!stmt) {
        return "";
    }
    stmt->bindString(1, file_id);
    auto res = stmt->query();
    if (!res || !res->next()) {
        return "";
    }
    return res->getString(0);
}

std::string GetFileUrlByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return "";
    }
    auto stmt = MySQLStmt::Create(db, "SELECT url FROM file_info WHERE md5 = ?");
    if (!stmt) {
        return "";
    }
    stmt->bindString(1, md5);
    auto res = stmt->query();
    if (!res || !res->next()) {
        return "";
    }
    return res->getString(0);
}

bool IncrementCount(MySQL::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }
    auto stmt = MySQLStmt::Create(db, "UPDATE file_info SET count = count + 1 WHERE md5 = ?");
    if (!stmt) {
        return false;
    }
    stmt->bindString(1, md5);
    return stmt->execute() == 0;
}

int DecrementCount(MySQL::ptr db, const std::string& file_id) {
    if (!db) {
        return -1;
    }
    auto stmt = MySQLStmt::Create(db,
        "UPDATE file_info SET count = count - 1 WHERE file_id = ? AND count > 0");
    if (!stmt) {
        return -1;
    }
    stmt->bindString(1, file_id);
    if (stmt->execute() != 0) {
        return -1;
    }
    auto res = db->query("SELECT count FROM file_info WHERE file_id = '%s'", file_id.c_str());
    if (!res || !res->next()) {
        return -1;
    }
    return res->getInt32(0);
}

std::vector<std::shared_ptr<FileInfo>> GetFileList(MySQL::ptr db, int offset, int limit) {
    std::vector<std::shared_ptr<FileInfo>> files;
    if (!db) {
        return files;
    }
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT id, md5, file_id, url, size, type, count, create_time, update_time FROM file_info ORDER BY id DESC LIMIT %d OFFSET %d", limit, offset);
    auto res = db->query(sql);
    if (!res) {
        return files;
    }
    while (res->next()) {
        std::shared_ptr<FileInfo> info;
        if (FileInfoFromResult(res, info)) {
            files.push_back(info);
        }
    }
    return files;
}

bool ExistsByMd5AndUser(MySQL::ptr db, const std::string& md5, const std::string& user) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db, "SELECT id FROM file_info WHERE md5 = ? AND url = ? LIMIT 1");
    if (!stmt) return false;
    stmt->bindString(1, md5);
    stmt->bindString(2, user);
    auto res = stmt->query();
    return res && res->next();
}

std::vector<std::shared_ptr<FileInfo>> GetFileListByUser(MySQL::ptr db, const std::string& user, int offset, int limit) {
    std::vector<std::shared_ptr<FileInfo>> files;
    if (!db) return files;
    auto stmt = MySQLStmt::Create(db,
        "SELECT id, md5, file_id, url,filename,size, type, count, create_time, update_time FROM file_info WHERE url = ? ORDER BY id DESC LIMIT ? OFFSET ?");
    if (!stmt) return files;
    stmt->bindString(1, user);
    stmt->bindInt32(2, limit);
    stmt->bindInt32(3, offset);
    auto res = stmt->query();
    if (!res) return files;
    while (res->next()) {
        std::shared_ptr<FileInfo> info;
        if (FileInfoFromResult(res, info)) {
            files.push_back(info);
        }
    }
    return files;
}

bool DeleteFileRecordByUserAndFilename(MySQL::ptr db, const std::string& user, const std::string& filename) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db, "DELETE FROM file_info WHERE url = ? AND filename = ?");
    if (!stmt) return false;
    stmt->bindString(1, user);
    stmt->bindString(2, filename);
    return stmt->execute() == 0;
}

}

namespace file_shared {

bool ExistsByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db, "SELECT file_md5 FROM file_shared WHERE file_md5 = ? LIMIT 1");
    if (!stmt) return false;
    stmt->bindString(1, md5);
    auto res = stmt->query();
    return res && res->next();
}

std::string GetFileIdByMd5(MySQL::ptr db, const std::string& md5) {
    if (!db) return "";
    auto stmt = MySQLStmt::Create(db, "SELECT file_id FROM file_shared WHERE file_md5 = ?");
    if (!stmt) return "";
    stmt->bindString(1, md5);
    auto res = stmt->query();
    if (!res || !res->next()) return "";
    return res->getString(0);
}

bool CreateShared(MySQL::ptr db, const std::string& md5, const std::string& file_id, int64_t file_size) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db,
        "INSERT INTO file_shared (file_md5, file_id, file_size, ref_count) VALUES (?, ?, ?, 1)");
    if (!stmt) return false;
    stmt->bindString(1, md5);
    stmt->bindString(2, file_id);
    stmt->bindInt64(3, file_size);
    return stmt->execute() == 0;
}

bool IncrementRef(MySQL::ptr db, const std::string& md5) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db, "UPDATE file_shared SET ref_count = ref_count + 1 WHERE file_md5 = ?");
    if (!stmt) return false;
    stmt->bindString(1, md5);
    return stmt->execute() == 0;
}

int DecrementRef(MySQL::ptr db, const std::string& md5) {
    if (!db) return -1;
    // FIBER_LOG_INFO(g_logger) << "decrement ref" << md5;
    auto stmt = MySQLStmt::Create(db, "UPDATE file_shared SET ref_count = ref_count - 1 WHERE file_md5 = ? AND ref_count > 0");
    stmt->bindString(1, md5);
    if (!stmt) return -1;
    if (stmt->execute() != 0) return -1;
    auto stmt2 = MySQLStmt::Create(db, "SELECT ref_count FROM file_shared WHERE file_md5 = ?");
    if (!stmt2) return -1;
    stmt2->bindString(1, md5);
    auto res = stmt2->query();
    if (!res || !res->next()) return -1;
    return res->getInt32(0);
}

bool DeleteShared(MySQL::ptr db, const std::string& md5) {
    if (!db) return false;
    auto stmt = MySQLStmt::Create(db, "DELETE FROM file_shared WHERE file_md5 = ?");
    if (!stmt) return false;
    stmt->bindString(1, md5);
    return stmt->execute() == 0;
}

}

}
