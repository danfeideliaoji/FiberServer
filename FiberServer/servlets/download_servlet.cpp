#include "download_servlet.h"
#include "FiberServer/db/db_executor.h"
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
    std::string& user=query["user"];
    std::string& filename=query["filename"]; 
    PerfTimer db_timer;
    struct DbResult {
        bool ok = false;
        std::shared_ptr<FileInfo> info;
    };
    auto db_result = DbExecutorMgr::GetInstance()->submit([user, filename]() {
        DbResult result;
        SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
        if (!mysql) {
            return result;
        }
        result.ok = true;
        result.info = file_info::GetFileByUserAndFilename(mysql, user, filename);
        return result;
    });
    if (!db_result.ok) {
        perf.addDbMs(db_timer.elapsedMs());
        response->setStatus(HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("{\"code\":1,\"msg\":\"db error\"}");
        return 0;
    }

    auto info = db_result.info;
    perf.addDbMs(db_timer.elapsedMs());
    if (!info) {
        perf.setStatus("not_found");
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("{\"code\":1,\"msg\":\"file not found\"}");
        return 0;
    }

    response->setStatus(HttpStatus::OK);
    response->setHeader("X-Accel-Redirect", "/" + info->file_id);
    response->setHeader("Content-Type", "application/octet-stream");

    // URL-encode filename for Content-Disposition (RFC 5987)
    response->setHeader("Content-Disposition",
        "attachment; filename*=UTF-8''" + filename);

    FIBER_LOG_INFO(g_logger)<<"file id: "<<info->file_id;
    perf.setStatus("ok");
    return 0;
}

}
}
