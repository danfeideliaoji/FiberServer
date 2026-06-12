#include "md5_servlet.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"
#include <exception>

namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

Md5Servlet::Md5Servlet()
    :Servlet("Md5Servlet") {
}

enum Md5Code {
    Success = 0,           // 秒传成功
    FileExists = 1,        // 文件已存在（用户已有此文件）
    NotExists = 2,         // 文件不存在，需要上传
    OtherError = 3,
};

int32_t Md5Servlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body) || !json.isObject()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"json parse error\"}", OtherError));
            return -1;
        }
        
        std::string username = JsonUtil::GetString(json, "username");
        std::string md5 = JsonUtil::GetString(json, "md5");
        std::string filename = JsonUtil::GetString(json, "filename");
        
        if (username.empty() || md5.empty()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, md5 are required\"}", OtherError));
            return -1;
        }
        
        FIBER_LOG_INFO(g_logger) << "md5 check: user=" << username << ", md5=" << md5 << ", filename=" << filename;
        
        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
        if (!mysql) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }
        
        // 检查服务器是否已有此md5的文件（秒传核心）
        if (file_info::ExistsByMd5(mysql, md5)) {
            FIBER_LOG_INFO(g_logger) << "md5 exists, instant upload success";
            // 秒传成功：增加引用计数
            file_info::IncrementCount(mysql, md5);
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"instant upload success\"}", Success));
            return 0;
        }
        
        // 文件不存在，需要上传
        FIBER_LOG_INFO(g_logger) << "md5 not found, need upload";
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"file not exists, need upload\"}", NotExists));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}

}
}
