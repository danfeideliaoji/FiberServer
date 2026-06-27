#include "md5_servlet.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"
#include "FiberServer/util/perf_util.h"
#include <exception>

namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

Md5Servlet::Md5Servlet()
    :Servlet("Md5Servlet") {
}

enum Md5Code {
    Success = 0,
    FileExists = 1,
    NotExists = 2,
    OtherError = 3,
};

int32_t Md5Servlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    ScopedPerfLog perf("/api/md5");
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body) || !json.isObject()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"json parse error\"}", OtherError));
            return -1;
        }
        
        auto meta = ArtifactMetadata::FromJson(json);
        std::string username = meta.owner;
        std::string md5 = meta.checksum;
        std::string filename = meta.storage_name;

        if (username.empty() || md5.empty()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"project_name/username, checksum/md5 are required\"}", OtherError));
            return -1;
        }
        
        FIBER_LOG_INFO(g_logger) << "md5 check: user=" << username << ", md5=" << md5 << ", filename=" << filename;
        
        PerfTimer db_timer;
        struct DbResult {
            bool ok = false;
            bool exists = false;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([md5]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if (!mysql) {
                return result;
            }
            result.ok = true;
            result.exists = file_shared::ExistsByMd5(mysql, md5);
            return result;
        });
        perf.addDbMs(db_timer.elapsedMs());
        if (!db_result.ok) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }

        if (db_result.exists) {
            FIBER_LOG_INFO(g_logger) << "md5 exists, instant upload success";
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"instant upload success\"}", Success));
            perf.setStatus("instant");
            return 0;
        }

        FIBER_LOG_INFO(g_logger) << "md5 not found, need upload";
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"file not exists, need upload\"}", NotExists));
        perf.setStatus("not_exists");
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}

}
}
