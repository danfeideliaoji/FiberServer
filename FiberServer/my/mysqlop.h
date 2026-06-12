#pragma once
#include "FiberServer/db/mysql.h"
#ifdef FIBERSERVER_USE_SOCI
#include "FiberServer/db/soci_db.h"
#endif
#include <string>
#include <vector>

namespace FiberServer{

struct UserInfo {
    int64_t id;
    std::string username;
    std::string password;
    std::string salt;
    std::string nickname;
    int8_t status;
    time_t last_login;
    time_t create_time;
    time_t update_time;
};

struct FileInfo {
    int64_t id;
    std::string md5;
    std::string file_id;
    std::string url;      // 复用为用户名
    std::string filename;
    int64_t size;
    std::string type;
    int count;
    time_t create_time;
    time_t update_time;
};

namespace user_info{

bool CreateUser(MySQL::ptr db, const std::string& username,
                const std::string& password, const std::string& salt,
                const std::string& nickname, int64_t& out_id);

#ifdef FIBERSERVER_USE_SOCI
bool CreateUser(SociDB::ptr db, const std::string& username,
                const std::string& password, const std::string& salt,
                const std::string& nickname, int64_t& out_id);
#endif

std::shared_ptr<UserInfo> GetUserById(MySQL::ptr db, int64_t id);

std::shared_ptr<UserInfo> GetUserByUsername(MySQL::ptr db, const std::string& username);

#ifdef FIBERSERVER_USE_SOCI
std::shared_ptr<UserInfo> GetUserByUsername(SociDB::ptr db, const std::string& username);
#endif

bool UpdatePassword(MySQL::ptr db, int64_t user_id,
                    const std::string& password, const std::string& salt);

bool UpdateLastLogin(MySQL::ptr db, int64_t user_id);

bool UpdateNickname(MySQL::ptr db, int64_t user_id, const std::string& nickname);

bool UpdateStatus(MySQL::ptr db, int64_t user_id, int8_t status);

bool DeleteUser(MySQL::ptr db, int64_t user_id);

std::vector<std::shared_ptr<UserInfo>> GetUsersByStatus(MySQL::ptr db, int8_t status);

int64_t GetUserCount(MySQL::ptr db);

}

namespace file_info{

bool ExistsByMd5(MySQL::ptr db, const std::string& md5);

bool ExistsByFileId(MySQL::ptr db, const std::string& file_id);

// 检查某用户是否已有此 md5 的文件（url 字段存用户名）
bool ExistsByMd5AndUser(MySQL::ptr db, const std::string& md5, const std::string& user);

bool CreateFile(MySQL::ptr db, const std::string& md5, const std::string& file_id,
                const std::string& url, const std::string& filename, int64_t size, const std::string& type);

bool UpdateFile(MySQL::ptr db, const std::string& file_id,
                const std::string& url, int64_t size, const std::string& type);

bool DeleteFileRecord(MySQL::ptr db, const std::string& file_id);

bool DeleteFileRecordByMd5(MySQL::ptr db, const std::string& md5);

bool DeleteFileRecordByUserAndFilename(MySQL::ptr db, const std::string& user, const std::string& filename);

int64_t GetTotalStorageSize(MySQL::ptr db);

std::shared_ptr<FileInfo> GetFileById(MySQL::ptr db, const std::string& file_id);

std::shared_ptr<FileInfo> GetFileByMd5(MySQL::ptr db, const std::string& md5);
std::shared_ptr<FileInfo> GetFileByUserAndFilename(MySQL::ptr db, const std::string& user, const std::string& filename);

int64_t GetFileSize(MySQL::ptr db, const std::string& file_id);

int64_t GetFileSizeByMd5(MySQL::ptr db, const std::string& md5);

std::string GetFileUrl(MySQL::ptr db, const std::string& file_id);

std::string GetFileUrlByMd5(MySQL::ptr db, const std::string& md5);

bool IncrementCount(MySQL::ptr db, const std::string& md5);

int DecrementCount(MySQL::ptr db, const std::string& file_id);

std::vector<std::shared_ptr<FileInfo>> GetFileList(MySQL::ptr db, int offset, int limit);

// 按用户名查询文件列表（url 字段存用户名）
std::vector<std::shared_ptr<FileInfo>> GetFileListByUser(MySQL::ptr db, const std::string& user, int offset, int limit);

}

namespace file_shared {

bool ExistsByMd5(MySQL::ptr db, const std::string& md5);

std::string GetFileIdByMd5(MySQL::ptr db, const std::string& md5);

bool CreateShared(MySQL::ptr db, const std::string& md5, const std::string& file_id, int64_t file_size);

bool IncrementRef(MySQL::ptr db, const std::string& md5);

int DecrementRef(MySQL::ptr db, const std::string& md5);

bool DeleteShared(MySQL::ptr db, const std::string& md5);

}

}
