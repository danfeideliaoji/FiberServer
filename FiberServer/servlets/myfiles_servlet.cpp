#include "myfiles_servlet.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/perf_util.h"
#include <json/value.h>
#include <exception>

namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

MyFilesServlet::MyFilesServlet()
    :Servlet("MyFilesServlet") {
}

int32_t MyFilesServlet::handle(http::HttpRequest::ptr request,
                               http::HttpResponse::ptr response,
                               http::HttpSession::ptr session) {
    ScopedPerfLog perf(request->getPath());
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body) || !json.isObject()) {
            response->setBody("{\"code\":1,\"msg\":\"json parse error\"}");
            return 0;
        }

        auto request_meta = ArtifactMetadata::FromJson(json);
        std::string username = request_meta.owner;
        if (username.empty()) {
            response->setBody("{\"code\":1,\"msg\":\"project_name is required\"}");
            return 0;
        }
        int offset = JsonUtil::GetInt32(json, "offset", 0);
        int limit = JsonUtil::GetInt32(json, "limit", 100);
        if (offset < 0) {
            offset = 0;
        }
        if (limit <= 0) {
            limit = 100;
        } else if (limit > 1000) {
            limit = 1000;
        }

        PerfTimer db_timer;
        struct DbResult {
            bool ok = false;
            std::vector<std::shared_ptr<ArtifactInfo>> artifacts;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([username, offset, limit]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if (!mysql) {
                return result;
            }
            result.ok = true;
            result.artifacts = artifact_info::GetArtifactsByProject(mysql, username, offset, limit);
            return result;
        });
        if (!db_result.ok) {
            perf.addDbMs(db_timer.elapsedMs());
            FIBER_LOG_ERROR(g_logger) << "mysql connection error";
            response->setBody("{\"code\":1,\"msg\":\"mysql connection error\"}");
            return 0;
        }

        perf.addDbMs(db_timer.elapsedMs());
        
        Json::Value result;
        result["code"] = 0;
        result["msg"] = "success";
        result["offset"] = offset;
        result["limit"] = limit;
        
        Json::Value artifactsArray(Json::arrayValue);
        for (const auto& artifact : db_result.artifacts) {
            Json::Value artifactJson;
            artifactJson["id"] = artifact->id;
            artifactJson["file_id"] = artifact->file_id;
            artifactJson["project_name"] = artifact->project_name;
            artifactJson["checksum"] = artifact->checksum;
            artifactJson["artifact_name"] = artifact->artifact_name;
            artifactJson["storage_name"] = ArtifactMetadata::BuildStorageName(
                artifact->artifact_name, artifact->version, artifact->build_no);
            artifactJson["version"] = artifact->version;
            artifactJson["build_no"] = artifact->build_no;
            artifactJson["branch"] = artifact->branch;
            artifactJson["commit_id"] = artifact->commit_id;
            artifactJson["size"] = artifact->size;
            artifactJson["artifact_type"] = artifact->artifact_type;
            artifactsArray.append(artifactJson);
        }
        result["artifacts"] = artifactsArray;
        response->setBody(JsonUtil::ToString(result));
        perf.setStatus("ok");
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody("{\"code\":1,\"msg\":\"internal error\"}");
        return 0;
    }
}

}
}
