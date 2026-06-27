#pragma once

#include "FiberServer/db/soci_db.h"

#include <memory>
#include <string>
#include <vector>

namespace FiberServer {

struct FileInfo {
    int64_t id;
    std::string md5;
    std::string file_id;
    std::string owner;
    std::string filename;
    int64_t size;
    std::string type;
    int count;
    time_t create_time;
    time_t update_time;
};

struct ArtifactInfo {
    int64_t id = 0;
    std::string project_name;
    std::string version;
    std::string build_no;
    std::string artifact_name;
    std::string checksum;
    std::string file_id;
    int64_t size = 0;
    std::string artifact_type;
    std::string branch;
    std::string commit_id;
    time_t create_time = 0;
    time_t update_time = 0;
};

namespace file_info {

bool ExistsByMd5(SociDB::ptr db, const std::string& md5);
bool ExistsByFileId(SociDB::ptr db, const std::string& file_id);
bool ExistsByMd5AndUser(SociDB::ptr db, const std::string& md5, const std::string& user);
bool CreateFile(SociDB::ptr db, const std::string& md5, const std::string& file_id,
                const std::string& owner, const std::string& filename, int64_t size, const std::string& type);
bool UpdateFile(SociDB::ptr db, const std::string& file_id,
                const std::string& owner, int64_t size, const std::string& type);
bool DeleteFileRecord(SociDB::ptr db, const std::string& file_id);
bool DeleteFileRecordByMd5(SociDB::ptr db, const std::string& md5);
bool DeleteFileRecordByUserAndFilename(SociDB::ptr db, const std::string& user, const std::string& filename);
int64_t GetTotalStorageSize(SociDB::ptr db);
std::shared_ptr<FileInfo> GetFileById(SociDB::ptr db, const std::string& file_id);
std::shared_ptr<FileInfo> GetFileByMd5(SociDB::ptr db, const std::string& md5);
int64_t GetFileSize(SociDB::ptr db, const std::string& file_id);
int64_t GetFileSizeByMd5(SociDB::ptr db, const std::string& md5);
std::string GetFileOwner(SociDB::ptr db, const std::string& file_id);
std::string GetFileOwnerByMd5(SociDB::ptr db, const std::string& md5);
bool IncrementCount(SociDB::ptr db, const std::string& md5);
int DecrementCount(SociDB::ptr db, const std::string& file_id);
std::vector<std::shared_ptr<FileInfo>> GetFileList(SociDB::ptr db, int offset, int limit);

}

namespace artifact_info {

bool CreateArtifact(SociDB::ptr db, const ArtifactInfo& artifact);
std::shared_ptr<ArtifactInfo> GetArtifact(SociDB::ptr db,
                                          const std::string& project_name,
                                          const std::string& version,
                                          const std::string& build_no,
                                          const std::string& artifact_name);
bool DeleteArtifact(SociDB::ptr db,
                    const std::string& project_name,
                    const std::string& version,
                    const std::string& build_no,
                    const std::string& artifact_name);
std::vector<std::shared_ptr<ArtifactInfo>> GetArtifactsByProject(SociDB::ptr db,
                                                                 const std::string& project_name,
                                                                 int offset,
                                                                 int limit);
std::shared_ptr<ArtifactInfo> GetLatestArtifact(SociDB::ptr db,
                                                const std::string& project_name);
std::vector<std::string> GetVersionsByProject(SociDB::ptr db,
                                              const std::string& project_name);
std::vector<std::string> GetBuildsByVersion(SociDB::ptr db,
                                            const std::string& project_name,
                                            const std::string& version);

}

namespace project_token {

bool CreateOrUpdateToken(SociDB::ptr db,
                         const std::string& project_name,
                         const std::string& token);
bool ValidateToken(SociDB::ptr db,
                   const std::string& project_name,
                   const std::string& token);

}

namespace file_shared {

bool ExistsByMd5(SociDB::ptr db, const std::string& md5);
std::string GetFileIdByMd5(SociDB::ptr db, const std::string& md5);
bool CreateShared(SociDB::ptr db, const std::string& md5, const std::string& file_id, int64_t file_size);
bool IncrementRef(SociDB::ptr db, const std::string& md5);
int DecrementRef(SociDB::ptr db, const std::string& md5);
bool DeleteShared(SociDB::ptr db, const std::string& md5);

}

}
