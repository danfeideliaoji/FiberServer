#include "mysqlop.h"

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
    info->owner = row.get<std::string>("owner");
    info->filename = row.get<std::string>("filename", "");
    info->size = row.get<long long>("size");
    info->type = row.get<std::string>("type", "");
    info->count = row.get<int>("count", 0);
    info->create_time = 0;
    info->update_time = 0;
    return info;
}

static std::shared_ptr<ArtifactInfo> ArtifactInfoFromRow(const soci::row& row) {
    std::shared_ptr<ArtifactInfo> info(new ArtifactInfo());
    info->id = row.get<long long>("id");
    info->project_name = row.get<std::string>("project_name");
    info->version = row.get<std::string>("version", "");
    info->build_no = row.get<std::string>("build_no", "");
    info->artifact_name = row.get<std::string>("artifact_name");
    info->checksum = row.get<std::string>("checksum");
    info->file_id = row.get<std::string>("file_id");
    info->size = row.get<long long>("size");
    info->artifact_type = row.get<std::string>("artifact_type", "");
    info->branch = row.get<std::string>("branch", "");
    info->commit_id = row.get<std::string>("commit_id", "");
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

std::shared_ptr<UserInfo> GetUserById(SociDB::ptr db, int64_t id) {
    if (!db) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT id, username, password, salt, nickname, status "
               "FROM user_info WHERE id = :id",
               soci::use(id, "id"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return UserInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetUserById error: " << e.what();
        return nullptr;
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

bool UpdatePassword(SociDB::ptr db, int64_t user_id,
                    const std::string& password, const std::string& salt) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE user_info SET password = :password, salt = :salt WHERE id = :id",
                         soci::use(password, "password"),
                         soci::use(salt, "salt"),
                         soci::use(user_id, "id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI UpdatePassword error: " << e.what();
        return false;
    }
}

bool UpdateLastLogin(SociDB::ptr db, int64_t user_id) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE user_info SET last_login = NOW() WHERE id = :id",
                         soci::use(user_id, "id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI UpdateLastLogin error: " << e.what();
        return false;
    }
}

bool UpdateNickname(SociDB::ptr db, int64_t user_id, const std::string& nickname) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE user_info SET nickname = :nickname WHERE id = :id",
                         soci::use(nickname, "nickname"),
                         soci::use(user_id, "id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI UpdateNickname error: " << e.what();
        return false;
    }
}

bool UpdateStatus(SociDB::ptr db, int64_t user_id, int8_t status) {
    if (!db) {
        return false;
    }

    int status_value = status;
    try {
        db->session() << "UPDATE user_info SET status = :status WHERE id = :id",
                         soci::use(status_value, "status"),
                         soci::use(user_id, "id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI UpdateStatus error: " << e.what();
        return false;
    }
}

bool DeleteUser(SociDB::ptr db, int64_t user_id) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM user_info WHERE id = :id",
                         soci::use(user_id, "id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteUser error: " << e.what();
        return false;
    }
}

std::vector<std::shared_ptr<UserInfo>> GetUsersByStatus(SociDB::ptr db, int8_t status) {
    std::vector<std::shared_ptr<UserInfo>> users;
    if (!db) {
        return users;
    }

    int status_value = status;
    try {
        auto& sql = db->session();
        soci::rowset<soci::row> rows =
            (sql.prepare << "SELECT id, username, password, salt, nickname, status "
                            "FROM user_info WHERE status = :status",
                            soci::use(status_value, "status"));
        for (const auto& row : rows) {
            users.push_back(UserInfoFromRow(row));
        }
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetUsersByStatus error: " << e.what();
    }

    return users;
}

int64_t GetUserCount(SociDB::ptr db) {
    if (!db) {
        return 0;
    }

    try {
        long long count = 0;
        db->session() << "SELECT COUNT(*) FROM user_info",
                         soci::into(count);
        return count;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetUserCount error: " << e.what();
        return 0;
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

bool ExistsByFileId(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return false;
    }

    try {
        auto& sql = db->session();
        int id = 0;
        sql << "SELECT id FROM file_shared WHERE file_id = :file_id LIMIT 1",
               soci::use(file_id, "file_id"),
               soci::into(id);
        return sql.got_data();
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI ExistsByFileId error: " << e.what();
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
        sql << "SELECT id FROM file_info FORCE INDEX (idx_owner_md5) "
               "WHERE owner = :user AND md5 = :md5 LIMIT 1",
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
                const std::string& owner, const std::string& filename,
                int64_t size, const std::string& type) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "INSERT INTO file_info (md5, file_id, owner, filename, size, type, count) "
                         "VALUES (:md5, :file_id, :owner, :filename, :size, :type, 1)",
                         soci::use(md5, "md5"),
                         soci::use(file_id, "file_id"),
                         soci::use(owner, "owner"),
                         soci::use(filename, "filename"),
                         soci::use(size, "size"),
                         soci::use(type, "type");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI CreateFile error: " << e.what();
        return false;
    }
}

bool UpdateFile(SociDB::ptr db, const std::string& file_id,
                const std::string& owner, int64_t size, const std::string& type) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "UPDATE file_info SET owner = :owner, size = :size, type = :type "
                         "WHERE file_id = :file_id",
                         soci::use(owner, "owner"),
                         soci::use(size, "size"),
                         soci::use(type, "type"),
                         soci::use(file_id, "file_id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI UpdateFile error: " << e.what();
        return false;
    }
}

bool DeleteFileRecord(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM file_info WHERE file_id = :file_id",
                         soci::use(file_id, "file_id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteFileRecord error: " << e.what();
        return false;
    }
}

bool DeleteFileRecordByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM file_info WHERE md5 = :md5",
                         soci::use(md5, "md5");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteFileRecordByMd5 error: " << e.what();
        return false;
    }
}

int64_t GetTotalStorageSize(SociDB::ptr db) {
    if (!db) {
        return 0;
    }

    try {
        long long size = 0;
        db->session() << "SELECT COALESCE(SUM(file_size), 0) FROM file_shared",
                         soci::into(size);
        return size;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetTotalStorageSize error: " << e.what();
        return 0;
    }
}

std::shared_ptr<FileInfo> GetFileById(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT fi.id, fi.md5, COALESCE(fs.file_id, fi.file_id) AS file_id, "
               "fi.owner, fi.filename, COALESCE(fs.file_size, fi.size) AS size, "
               "fi.type, COALESCE(fs.ref_count, fi.count) AS count, fi.create_time, fi.update_time "
               "FROM file_info fi LEFT JOIN file_shared fs ON fs.file_md5 = fi.md5 "
               "WHERE COALESCE(fs.file_id, fi.file_id) = :file_id LIMIT 1",
               soci::use(file_id, "file_id"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return FileInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileById error: " << e.what();
        return nullptr;
    }
}

std::shared_ptr<FileInfo> GetFileByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT fi.id, fi.md5, COALESCE(fs.file_id, fi.file_id) AS file_id, "
               "fi.owner, fi.filename, COALESCE(fs.file_size, fi.size) AS size, "
               "fi.type, COALESCE(fs.ref_count, fi.count) AS count, fi.create_time, fi.update_time "
               "FROM file_info fi LEFT JOIN file_shared fs ON fs.file_md5 = fi.md5 "
               "WHERE fi.md5 = :md5 LIMIT 1",
               soci::use(md5, "md5"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return FileInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileByMd5 error: " << e.what();
        return nullptr;
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
        sql << "SELECT id, md5, file_id, owner, filename, size, type, count, create_time, update_time "
               "FROM file_info FORCE INDEX (idx_owner_filename) "
               "WHERE owner = :user AND filename = :filename LIMIT 1",
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
            (sql.prepare << "SELECT id, md5, file_id, owner, filename, size, type, count, create_time, update_time "
                            "FROM file_info FORCE INDEX (idx_owner_id) "
                            "WHERE owner = :user ORDER BY id DESC LIMIT :limit OFFSET :offset",
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

int64_t GetFileSize(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return 0;
    }

    try {
        auto& sql = db->session();
        long long size = 0;
        sql << "SELECT file_size FROM file_shared WHERE file_id = :file_id",
               soci::use(file_id, "file_id"),
               soci::into(size);
        if (!sql.got_data()) {
            return 0;
        }
        return size;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileSize error: " << e.what();
        return 0;
    }
}

int64_t GetFileSizeByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return 0;
    }

    try {
        auto& sql = db->session();
        long long size = 0;
        sql << "SELECT file_size FROM file_shared WHERE file_md5 = :md5",
               soci::use(md5, "md5"),
               soci::into(size);
        if (!sql.got_data()) {
            return 0;
        }
        return size;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileSizeByMd5 error: " << e.what();
        return 0;
    }
}

std::string GetFileOwner(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return "";
    }

    try {
        auto& sql = db->session();
        std::string owner;
        sql << "SELECT owner FROM file_info WHERE file_id = :file_id",
               soci::use(file_id, "file_id"),
               soci::into(owner);
        if (!sql.got_data()) {
            return "";
        }
        return owner;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileOwner error: " << e.what();
        return "";
    }
}

std::string GetFileOwnerByMd5(SociDB::ptr db, const std::string& md5) {
    if (!db) {
        return "";
    }

    try {
        auto& sql = db->session();
        std::string owner;
        sql << "SELECT owner FROM file_info WHERE md5 = :md5",
               soci::use(md5, "md5"),
               soci::into(owner);
        if (!sql.got_data()) {
            return "";
        }
        return owner;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileOwnerByMd5 error: " << e.what();
        return "";
    }
}

bool DeleteFileRecordByUserAndFilename(SociDB::ptr db,
                                       const std::string& user,
                                       const std::string& filename) {
    if (!db) {
        return false;
    }

    try {
        db->session() << "DELETE FROM file_info WHERE owner = :user AND filename = :filename",
                         soci::use(user, "user"),
                         soci::use(filename, "filename");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteFileRecordByUserAndFilename error: " << e.what();
        return false;
    }
}

int DecrementCount(SociDB::ptr db, const std::string& file_id) {
    if (!db) {
        return -1;
    }

    try {
        auto& sql = db->session();
        sql << "UPDATE file_info SET count = count - 1 WHERE file_id = :file_id AND count > 0",
               soci::use(file_id, "file_id");
        int count = -1;
        sql << "SELECT count FROM file_info WHERE file_id = :file_id",
               soci::use(file_id, "file_id"),
               soci::into(count);
        if (!sql.got_data()) {
            return -1;
        }
        return count;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DecrementCount error: " << e.what();
        return -1;
    }
}

std::vector<std::shared_ptr<FileInfo>> GetFileList(SociDB::ptr db, int offset, int limit) {
    std::vector<std::shared_ptr<FileInfo>> files;
    if (!db) {
        return files;
    }

    try {
        auto& sql = db->session();
        soci::rowset<soci::row> rows =
            (sql.prepare << "SELECT fi.id, fi.md5, COALESCE(fs.file_id, fi.file_id) AS file_id, "
                            "fi.owner, fi.filename, COALESCE(fs.file_size, fi.size) AS size, "
                            "fi.type, COALESCE(fs.ref_count, fi.count) AS count, fi.create_time, fi.update_time "
                            "FROM file_info fi LEFT JOIN file_shared fs ON fs.file_md5 = fi.md5 "
                            "ORDER BY fi.id DESC LIMIT :limit OFFSET :offset",
                            soci::use(limit, "limit"),
                            soci::use(offset, "offset"));
        for (const auto& row : rows) {
            files.push_back(FileInfoFromRow(row));
        }
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetFileList error: " << e.what();
    }

    return files;
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

namespace artifact_info {

static bool EnsureArtifactTable(SociDB::ptr db) {
    if (!db) {
        return false;
    }
    try {
        db->session() << "CREATE TABLE IF NOT EXISTS artifact_info ("
                         "id BIGINT AUTO_INCREMENT PRIMARY KEY, "
                         "project_name VARCHAR(128) NOT NULL, "
                         "version VARCHAR(128) NOT NULL DEFAULT '', "
                         "build_no VARCHAR(128) NOT NULL DEFAULT '', "
                         "artifact_name VARCHAR(256) NOT NULL, "
                         "checksum VARCHAR(128) NOT NULL, "
                         "file_id VARCHAR(512) NOT NULL, "
                         "size BIGINT NOT NULL DEFAULT 0, "
                         "artifact_type VARCHAR(128) NOT NULL DEFAULT '', "
                         "branch VARCHAR(128) NOT NULL DEFAULT '', "
                         "commit_id VARCHAR(128) NOT NULL DEFAULT '', "
                         "create_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                         "update_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
                         "UNIQUE KEY uk_project_version_build_name (project_name, version, build_no, artifact_name), "
                         "INDEX idx_project_id (project_name, id), "
                         "INDEX idx_project_version (project_name, version), "
                         "INDEX idx_checksum (checksum), "
                         "INDEX idx_file_id (file_id)"
                         ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI EnsureArtifactTable error: " << e.what();
        return false;
    }
}

bool CreateArtifact(SociDB::ptr db, const ArtifactInfo& artifact) {
    if (!EnsureArtifactTable(db)) {
        return false;
    }

    try {
        db->session() << "INSERT INTO artifact_info "
                         "(project_name, version, build_no, artifact_name, checksum, file_id, "
                         "size, artifact_type, branch, commit_id) "
                         "VALUES (:project_name, :version, :build_no, :artifact_name, :checksum, :file_id, "
                         ":size, :artifact_type, :branch, :commit_id) "
                         "ON DUPLICATE KEY UPDATE "
                         "checksum = VALUES(checksum), file_id = VALUES(file_id), size = VALUES(size), "
                         "artifact_type = VALUES(artifact_type), branch = VALUES(branch), "
                         "commit_id = VALUES(commit_id), update_time = CURRENT_TIMESTAMP",
                         soci::use(artifact.project_name, "project_name"),
                         soci::use(artifact.version, "version"),
                         soci::use(artifact.build_no, "build_no"),
                         soci::use(artifact.artifact_name, "artifact_name"),
                         soci::use(artifact.checksum, "checksum"),
                         soci::use(artifact.file_id, "file_id"),
                         soci::use(artifact.size, "size"),
                         soci::use(artifact.artifact_type, "artifact_type"),
                         soci::use(artifact.branch, "branch"),
                         soci::use(artifact.commit_id, "commit_id");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI CreateArtifact error: " << e.what();
        return false;
    }
}

std::shared_ptr<ArtifactInfo> GetArtifact(SociDB::ptr db,
                                          const std::string& project_name,
                                          const std::string& version,
                                          const std::string& build_no,
                                          const std::string& artifact_name) {
    if (!EnsureArtifactTable(db)) {
        return nullptr;
    }

    try {
        auto& sql = db->session();
        soci::row row;
        sql << "SELECT id, project_name, version, build_no, artifact_name, checksum, file_id, "
               "size, artifact_type, branch, commit_id, create_time, update_time "
               "FROM artifact_info WHERE project_name = :project_name AND version = :version "
               "AND build_no = :build_no AND artifact_name = :artifact_name LIMIT 1",
               soci::use(project_name, "project_name"),
               soci::use(version, "version"),
               soci::use(build_no, "build_no"),
               soci::use(artifact_name, "artifact_name"),
               soci::into(row);
        if (!sql.got_data()) {
            return nullptr;
        }
        return ArtifactInfoFromRow(row);
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetArtifact error: " << e.what();
        return nullptr;
    }
}

bool DeleteArtifact(SociDB::ptr db,
                    const std::string& project_name,
                    const std::string& version,
                    const std::string& build_no,
                    const std::string& artifact_name) {
    if (!EnsureArtifactTable(db)) {
        return false;
    }

    try {
        db->session() << "DELETE FROM artifact_info WHERE project_name = :project_name "
                         "AND version = :version AND build_no = :build_no "
                         "AND artifact_name = :artifact_name",
                         soci::use(project_name, "project_name"),
                         soci::use(version, "version"),
                         soci::use(build_no, "build_no"),
                         soci::use(artifact_name, "artifact_name");
        return true;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI DeleteArtifact error: " << e.what();
        return false;
    }
}

std::vector<std::shared_ptr<ArtifactInfo>> GetArtifactsByProject(SociDB::ptr db,
                                                                 const std::string& project_name,
                                                                 int offset,
                                                                 int limit) {
    std::vector<std::shared_ptr<ArtifactInfo>> artifacts;
    if (!EnsureArtifactTable(db)) {
        return artifacts;
    }

    try {
        auto& sql = db->session();
        soci::rowset<soci::row> rows =
            (sql.prepare << "SELECT id, project_name, version, build_no, artifact_name, checksum, file_id, "
                            "size, artifact_type, branch, commit_id, create_time, update_time "
                            "FROM artifact_info FORCE INDEX (idx_project_id) "
                            "WHERE project_name = :project_name ORDER BY id DESC LIMIT :limit OFFSET :offset",
                            soci::use(project_name, "project_name"),
                            soci::use(limit, "limit"),
                            soci::use(offset, "offset"));
        for (const auto& row : rows) {
            artifacts.push_back(ArtifactInfoFromRow(row));
        }
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "SOCI GetArtifactsByProject error: " << e.what();
    }

    return artifacts;
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
