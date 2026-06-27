#include "artifact_metadata.h"

#include "FiberServer/util/json_util.h"

#include <json/value.h>
#include <vector>

namespace FiberServer {

namespace {

std::string firstNonEmpty(const Json::Value& json, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        auto value = JsonUtil::GetString(json, name);
        if (!value.empty()) {
            return value;
        }
    }
    return "";
}

std::string firstNonEmpty(const std::map<std::string, std::string>& params,
                          const std::vector<std::string>& names) {
    for (const auto& name : names) {
        auto it = params.find(name);
        if (it != params.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return "";
}

int64_t int64Param(const std::map<std::string, std::string>& params, const std::string& name) {
    auto it = params.find(name);
    if (it == params.end() || it->second.empty()) {
        return 0;
    }
    try {
        return std::stoll(it->second);
    } catch (...) {
        return 0;
    }
}

bool hasAny(const Json::Value& json, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (json.isMember(name)) {
            return true;
        }
    }
    return false;
}

bool hasAny(const std::map<std::string, std::string>& params,
            const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (params.find(name) != params.end()) {
            return true;
        }
    }
    return false;
}

}

ArtifactMetadata ArtifactMetadata::FromJson(const Json::Value& json) {
    auto owner = firstNonEmpty(json, {"project_name", "project", "namespace", "username", "user"});
    auto checksum = firstNonEmpty(json, {"checksum", "md5"});
    auto artifact_name = firstNonEmpty(json, {"artifact_name", "filename", "file_name"});
    auto type = firstNonEmpty(json, {"artifact_type", "type"});
    auto version = JsonUtil::GetString(json, "version");
    auto build_no = JsonUtil::GetString(json, "build_no");
    auto branch = JsonUtil::GetString(json, "branch");
    auto commit_id = JsonUtil::GetString(json, "commit_id");
    auto size = JsonUtil::GetInt64(json, "size");
    bool artifact_fields = hasAny(json, {"project_name", "project", "namespace", "checksum",
                                         "artifact_name", "artifact_type", "version", "build_no",
                                         "branch", "commit_id"});
    return FromFields(owner, checksum, artifact_name, size, type, version, build_no,
                      branch, commit_id, artifact_fields);
}

ArtifactMetadata ArtifactMetadata::FromParams(const std::map<std::string, std::string>& params) {
    auto owner = firstNonEmpty(params, {"project_name", "project", "namespace", "username", "user"});
    auto checksum = firstNonEmpty(params, {"checksum", "md5"});
    auto artifact_name = firstNonEmpty(params, {"artifact_name", "filename", "file_name"});
    auto type = firstNonEmpty(params, {"artifact_type", "type"});
    auto version = firstNonEmpty(params, {"version"});
    auto build_no = firstNonEmpty(params, {"build_no"});
    auto branch = firstNonEmpty(params, {"branch"});
    auto commit_id = firstNonEmpty(params, {"commit_id"});
    auto size = int64Param(params, "size");
    bool artifact_fields = hasAny(params, {"project_name", "project", "namespace", "checksum",
                                           "artifact_name", "artifact_type", "version", "build_no",
                                           "branch", "commit_id"});
    return FromFields(owner, checksum, artifact_name, size, type, version, build_no,
                      branch, commit_id, artifact_fields);
}

}
