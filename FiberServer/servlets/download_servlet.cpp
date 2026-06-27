#include "download_servlet.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"
#include "FiberServer/util/perf_util.h"

namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

DownloadServlet::DownloadServlet()
    :Servlet("DownloadServlet") {
}
static std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < query.size()) {
        size_t eq = query.find('=', pos);
        if (eq == std::string::npos) break;
        size_t amp = query.find('&', eq);
        if (amp == std::string::npos) amp = query.size();
        m[StringUtil::UrlDecode(query.substr(pos, eq - pos))] =
            StringUtil::UrlDecode(query.substr(eq + 1, amp - eq - 1));
        pos = amp + 1;
    }
    return m;
}
int32_t DownloadServlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    ScopedPerfLog perf("/api/download");
    std::string s = request->getQuery();
    auto query = parseQuery(s);
    auto meta = ArtifactMetadata::FromParams(query);
    std::string user = meta.owner;
    std::string filename = meta.storage_name;
    std::string download_name = meta.artifact_name.empty() ? filename : meta.artifact_name;
    bool artifact_request = request->getPath() == "/api/artifacts/download";
    PerfTimer db_timer;
    struct DbResult {
        bool ok = false;
        std::shared_ptr<FileInfo> info;
        std::shared_ptr<ArtifactInfo> artifact;
    };
    auto db_result = DbExecutorMgr::GetInstance()->submit([user, filename, meta, artifact_request]() {
        DbResult result;
        SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
        if (!mysql) {
            return result;
        }
        result.ok = true;
        if (artifact_request) {
            result.artifact = artifact_info::GetArtifact(mysql, meta.owner, meta.version,
                                                         meta.build_no, meta.artifact_name);
        } else {
            result.info = file_info::GetFileByUserAndFilename(mysql, user, filename);
        }
        return result;
    });
    if (!db_result.ok) {
        perf.addDbMs(db_timer.elapsedMs());
        response->setStatus(HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("{\"code\":1,\"msg\":\"db error\"}");
        return 0;
    }

    std::string file_id;
    if (artifact_request && db_result.artifact) {
        file_id = db_result.artifact->file_id;
        download_name = db_result.artifact->artifact_name;
    } else if (db_result.info) {
        file_id = db_result.info->file_id;
    }
    perf.addDbMs(db_timer.elapsedMs());
    if (file_id.empty()) {
        perf.setStatus("not_found");
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("{\"code\":1,\"msg\":\"file not found\"}");
        return 0;
    }

    response->setStatus(HttpStatus::OK);
    response->setHeader("X-Accel-Redirect", "/" + file_id);
    response->setHeader("Content-Type", "application/octet-stream");

    // URL-encode filename for Content-Disposition (RFC 5987)
    response->setHeader("Content-Disposition",
        "attachment; filename*=UTF-8''" + download_name);

    FIBER_LOG_INFO(g_logger)<<"file id: "<<file_id;
    perf.setStatus("ok");
    return 0;
}

}
}
