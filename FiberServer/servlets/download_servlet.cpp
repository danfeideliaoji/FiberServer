#include "download_servlet.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"

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
    std::string s = request->getQuery();
    auto query = parseQuery(s);
    std::string& user=query["user"];
    std::string& filename=query["filename"]; 
#ifdef FIBERSERVER_USE_SOCI
    SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
#else
    MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
#endif
    if (!mysql) {
        response->setStatus(HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("{\"code\":1,\"msg\":\"db error\"}");
        return 0;
    }

    auto info = file_info::GetFileByUserAndFilename(mysql,
         user, filename);
    if (!info) {
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
    return 0;
}

}
}
