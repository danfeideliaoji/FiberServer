#include "register_servlet.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/mysql.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/hash_util.h"
#include <exception>

namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

RegisterServlet::RegisterServlet()
    :Servlet("RegisterServlet") {
}

enum RegisterResult {
    Success = 0,
    OtherError = 1,
    UsernameExists = 2,
    NicknameExists = 3,
};

int32_t RegisterServlet::handle(http::HttpRequest::ptr request,
                                http::HttpResponse::ptr response,
                                http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body)) {
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"json parse error\"}", OtherError));
            return -1;
        }

        std::string username = JsonUtil::GetString(json, "username");
        std::string password = JsonUtil::GetString(json, "password");
        std::string nickname = JsonUtil::GetString(json, "nickname");

        if (username.empty() || password.empty() || nickname.empty()) {
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"username, password, nickname is required\"}",
                OtherError));
            return 0;
        }

        if (username.size() < 3 || username.size() > 16) {
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"username length must be between 3 and 16\"}",
                OtherError));
            return 0;
        }

#ifdef FIBERSERVER_USE_SOCI
        SociDB::ptr mysql = SociMgr::GetInstance()->get("user_info");
#else
        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("user_info");
#endif
        if (!mysql) {
            FIBER_LOG_ERROR(g_logger) << "mysql connection error";
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }

        if (user_info::GetUserByUsername(mysql, username)) {
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"username already exists\"}", UsernameExists));
            return 0;
        }

        std::string salt = random_string(16);
        std::string password_hash = md5(password + salt);

        int64_t user_id = 0;
        if (!user_info::CreateUser(mysql, username, password_hash, salt, nickname, user_id)) {
            FIBER_LOG_ERROR(g_logger) << "create user failed";
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"create user failed\"}", OtherError));
            return -1;
        }

        FIBER_LOG_INFO(g_logger) << "register success, user_id: " << user_id;
        Json::Value result;
        result["code"] = Success;
        result["msg"] = "register success";
        result["user_id"] = user_id;
        response->setBody(JsonUtil::ToString(result));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}

}
}
