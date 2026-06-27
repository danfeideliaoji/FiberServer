#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace Json {
class Value;
}

namespace FiberServer {

struct ArtifactMetadata {
    // 普通文件接口中表示用户名；artifact 接口中表示 project_name。
    std::string owner;
    // 兼容旧 md5 字段，也是制品去重和冲突判断的核心 key。
    std::string checksum;
    std::string artifact_name;
    // 兼容旧 file_info.filename，artifact 会拼成 version/build_no/name。
    std::string storage_name;
    std::string type;
    std::string version;
    std::string build_no;
    std::string branch;
    std::string commit_id;
    int64_t size = 0;
    bool artifact_mode = false;

    static ArtifactMetadata FromFields(const std::string& owner,
                                       const std::string& checksum,
                                       const std::string& artifact_name,
                                       int64_t size,
                                       const std::string& type,
                                       const std::string& version,
                                       const std::string& build_no,
                                       const std::string& branch = "",
                                       const std::string& commit_id = "",
                                       bool explicit_artifact_mode = false) {
        ArtifactMetadata meta;
        meta.owner = owner;
        meta.checksum = checksum;
        meta.artifact_name = artifact_name;
        meta.storage_name = BuildStorageName(artifact_name, version, build_no);
        meta.type = type;
        meta.version = version;
        meta.build_no = build_no;
        meta.branch = branch;
        meta.commit_id = commit_id;
        meta.size = size;
        meta.artifact_mode = explicit_artifact_mode || !version.empty() || !build_no.empty();
        return meta;
    }
    static ArtifactMetadata FromJson(const Json::Value& json);
    static ArtifactMetadata FromParams(const std::map<std::string, std::string>& params);
    static std::string BuildStorageName(const std::string& artifact_name,
                                        const std::string& version,
                                        const std::string& build_no) {
        if (version.empty()) {
            return artifact_name;
        }
        if (build_no.empty()) {
            return version + "/" + artifact_name;
        }
        return version + "/" + build_no + "/" + artifact_name;
    }
};

}
