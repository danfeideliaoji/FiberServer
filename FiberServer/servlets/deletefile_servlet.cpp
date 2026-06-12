#include "deletefile_servlet.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/my/fastdfs.h"
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
#ifdef FIBERSERVER_USE_SOCI
        SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
#else
        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
#endif
        MySQL::ptr shared_db = MySQLMgr::GetInstance()->get("file_shared");
        if(!mysql || !shared_db){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }
        auto fileInfo = file_info::GetFileByUserAndFilename(mysql, username, file_name);
        if(!fileInfo){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"file not found\"}", OtherError));
            return -1;
        }
        // 删除用户文件记录
        if(!file_info::DeleteFileRecordByUserAndFilename(mysql, username, file_name)){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"delete db record failed\"}", OtherError));
            return -1;
        }
        // 减少共享引用计数，为0时删除FastDFS文件和共享记录
        int ref = file_shared::DecrementRef(shared_db, fileInfo->md5);
        if(ref == 0){
            FastDFSUtil::deleteFile(fileInfo->file_id);
            file_shared::DeleteShared(shared_db, fileInfo->md5);
        }
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", Success));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}
}
}
