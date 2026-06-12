#include "myfiles_servlet.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/mysql.h"
#include "FiberServer/my/mysqlop.h"
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
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body) || !json.isObject()) {
            response->setBody("{\"code\":1,\"msg\":\"json parse error\"}");
            return 0;
        }

        std::string username = JsonUtil::GetString(json, "username");
        if (username.empty()) {
            response->setBody("{\"code\":1,\"msg\":\"username is required\"}");
            return 0;
        }

        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
        if (!mysql) {
            FIBER_LOG_ERROR(g_logger) << "mysql connection error";
            response->setBody("{\"code\":1,\"msg\":\"mysql connection error\"}");
            return 0;
        }

        // 按用户名查询文件列表（url 字段存用户名）
        auto files = file_info::GetFileListByUser(mysql, username, 0, 1000);
        
        Json::Value result;
        result["code"] = 0;
        result["msg"] = "success";
        
        Json::Value filesArray(Json::arrayValue);
        for (const auto& file : files) {
            Json::Value fileJson;
            fileJson["id"] = file->id;
            fileJson["file_id"] = file->file_id;
            fileJson["md5"] = file->md5;
            fileJson["size"] = file->size;
            fileJson["type"] = file->type;
            fileJson["filename"] = file->filename;
            filesArray.append(fileJson);
        }
        
        result["files"] = filesArray;
        response->setBody(JsonUtil::ToString(result));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody("{\"code\":1,\"msg\":\"internal error\"}");
        return 0;
    }
}

}
}
