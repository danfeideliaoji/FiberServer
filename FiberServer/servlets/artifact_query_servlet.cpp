#include "artifact_query_servlet.h"

#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/perf_util.h"

#include <exception>
#include <map>

namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

ArtifactQueryServlet::ArtifactQueryServlet()
    : Servlet("ArtifactQueryServlet") {
}

static std::map<std::string, std::string> collectParams(const HttpRequest::ptr& request) {
    std::map<std::string, std::string> params;
    for (const auto& key : {"project_name", "project", "namespace", "username", "user",
                            "version", "build_no", "artifact_name"}) {
        auto value = request->getParam(key);
        if (!value.empty()) {
            params[key] = value;
        }
    }
    return params;
}

static ArtifactMetadata metadataFromRequest(const HttpRequest::ptr& request) {
    auto meta = ArtifactMetadata::FromParams(collectParams(request));
    if (!meta.owner.empty()) {
        return meta;
    }

    Json::Value body;
    if (JsonUtil::FromString(body, request->getBody()) && body.isObject()) {
        return ArtifactMetadata::FromJson(body);
    }
    return meta;
}

static Json::Value artifactToJson(const ArtifactInfo& artifact) {
    Json::Value value;
    value["id"] = artifact.id;
    value["project_name"] = artifact.project_name;
    value["version"] = artifact.version;
    value["build_no"] = artifact.build_no;
    value["artifact_name"] = artifact.artifact_name;
    value["checksum"] = artifact.checksum;
    value["file_id"] = artifact.file_id;
    value["size"] = artifact.size;
    value["artifact_type"] = artifact.artifact_type;
    value["branch"] = artifact.branch;
    value["commit_id"] = artifact.commit_id;
    return value;
}

int32_t ArtifactQueryServlet::handle(http::HttpRequest::ptr request,
                                     http::HttpResponse::ptr response,
                                     http::HttpSession::ptr session) {
    ScopedPerfLog perf(request->getPath());
    response->setHeader("Content-Type", "text/json charset=utf-8");

    try {
        auto meta = metadataFromRequest(request);
        if (meta.owner.empty()) {
            response->setBody("{\"code\":1,\"msg\":\"project_name is required\"}");
            return 0;
        }
        if (request->getPath() == "/api/artifacts/builds" && meta.version.empty()) {
            response->setBody("{\"code\":1,\"msg\":\"version is required\"}");
            return 0;
        }

        PerfTimer db_timer;
        struct DbResult {
            bool ok = false;
            std::shared_ptr<ArtifactInfo> latest;
            std::vector<std::string> versions;
            std::vector<std::string> builds;
        };
        auto path = request->getPath();
        auto db_result = DbExecutorMgr::GetInstance()->submit([path, meta]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if (!mysql) {
                return result;
            }
            result.ok = true;
            if (path == "/api/artifacts/latest") {
                result.latest = artifact_info::GetLatestArtifact(mysql, meta.owner);
            } else if (path == "/api/artifacts/versions") {
                result.versions = artifact_info::GetVersionsByProject(mysql, meta.owner);
            } else if (path == "/api/artifacts/builds") {
                result.builds = artifact_info::GetBuildsByVersion(mysql, meta.owner, meta.version);
            }
            return result;
        });
        perf.addDbMs(db_timer.elapsedMs());

        if (!db_result.ok) {
            response->setBody("{\"code\":1,\"msg\":\"mysql connection error\"}");
            return 0;
        }

        Json::Value result;
        result["code"] = 0;
        result["msg"] = "success";
        if (path == "/api/artifacts/latest") {
            if (db_result.latest) {
                result["artifact"] = artifactToJson(*db_result.latest);
            } else {
                result["artifact"] = Json::Value(Json::nullValue);
            }
        } else if (path == "/api/artifacts/versions") {
            Json::Value versions(Json::arrayValue);
            for (const auto& version : db_result.versions) {
                versions.append(version);
            }
            result["versions"] = versions;
        } else if (path == "/api/artifacts/builds") {
            Json::Value builds(Json::arrayValue);
            for (const auto& build : db_result.builds) {
                builds.append(build);
            }
            result["builds"] = builds;
        }
        response->setBody(JsonUtil::ToString(result));
        perf.setStatus("ok");
        return 0;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody("{\"code\":1,\"msg\":\"internal error\"}");
        return 0;
    }
}

}
}
