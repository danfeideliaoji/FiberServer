#include "artifact_token_servlet.h"

#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/perf_util.h"

#include <exception>

namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

ArtifactTokenServlet::ArtifactTokenServlet()
    : Servlet("ArtifactTokenServlet") {
}

int32_t ArtifactTokenServlet::handle(http::HttpRequest::ptr request,
                                     http::HttpResponse::ptr response,
                                     http::HttpSession::ptr session) {
    ScopedPerfLog perf("/api/artifacts/token");
    response->setHeader("Content-Type", "text/json charset=utf-8");

    try {
        Json::Value json;
        if (!JsonUtil::FromString(json, request->getBody()) || !json.isObject()) {
            response->setBody("{\"code\":1,\"msg\":\"json parse error\"}");
            return -1;
        }

        auto meta = ArtifactMetadata::FromJson(json);
        auto token = JsonUtil::GetString(json, "token");
        if (meta.owner.empty() || token.empty()) {
            response->setBody("{\"code\":1,\"msg\":\"project_name and token are required\"}");
            return -1;
        }

        PerfTimer db_timer;
        auto ok = DbExecutorMgr::GetInstance()->submit([meta, token]() {
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            return project_token::CreateOrUpdateToken(mysql, meta.owner, token);
        });
        perf.addDbMs(db_timer.elapsedMs());

        if (!ok) {
            response->setBody("{\"code\":1,\"msg\":\"create artifact token failed\"}");
            return -1;
        }
        response->setBody("{\"code\":0,\"msg\":\"success\"}");
        perf.setStatus("ok");
        return 0;
    } catch (const std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody("{\"code\":1,\"msg\":\"internal error\"}");
        return -1;
    }
}

}
}
