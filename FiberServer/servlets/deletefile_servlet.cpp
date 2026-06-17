#include "deletefile_servlet.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/util/perf_util.h"
#include <exception>
namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

enum DeleteFileCode{
    Success = 0,
    OtherError = 1,
};

DeleteFileServlet::DeleteFileServlet()
    :Servlet("DeleteFileServlet") {
}
//url /api/deletefile
int32_t DeleteFileServlet::handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session)  {
    ScopedPerfLog perf("/api/deletefile");
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;
        if(!JsonUtil::FromString(json, body)){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"parse body failed\"}", OtherError));
            return -1;
        }
        std::string username = JsonUtil::GetString(json, "user");
        std::string file_name = JsonUtil::GetString(json, "file_name");
        if(username.empty() || file_name.empty()){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, file_name are required\"}", OtherError));
            return -1;
        }
        PerfTimer db_timer;
        struct DbResult {
            bool ok = false;
            bool not_found = false;
            bool delete_physical = false;
            std::string file_id;
            std::string message;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([username, file_name]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if(!mysql){
                result.message = "mysql connection error";
                return result;
            }
            auto fileInfo = file_info::GetFileByUserAndFilename(mysql, username, file_name);
            if(!fileInfo){
                result.not_found = true;
                result.message = "file not found";
                return result;
            }
            soci::transaction tr(mysql->session());
            if(!file_info::DeleteFileRecordByUserAndFilename(mysql, username, file_name)){
                tr.rollback();
                result.message = "delete db record failed";
                return result;
            }
            int ref = file_shared::DecrementRef(mysql, fileInfo->md5);
            if(ref < 0) {
                tr.rollback();
                result.message = "decrement shared ref failed";
                return result;
            }
            if(ref == 0 && !file_shared::DeleteShared(mysql, fileInfo->md5)) {
                tr.rollback();
                result.message = "delete shared record failed";
                return result;
            }
            tr.commit();
            result.ok = true;
            result.delete_physical = ref == 0;
            result.file_id = fileInfo->file_id;
            return result;
        });
        perf.addDbMs(db_timer.elapsedMs());
        if(!db_result.ok){
            if (db_result.not_found) {
                perf.setStatus("not_found");
            }
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"%s\"}", OtherError, db_result.message.c_str()));
            return -1;
        }
        if(db_result.delete_physical){
            PerfTimer fastdfs_timer;
            FastDFSUtil::deleteFile(db_result.file_id);
            perf.addFastDfsMs(fastdfs_timer.elapsedMs());
        }
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", Success));
        perf.setStatus("ok");
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}
}
}
