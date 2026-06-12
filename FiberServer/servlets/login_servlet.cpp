#include "login_servlet.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/hash_util.h"
#include <json/value.h>
#include <exception>
#include "FiberServer/fiber.h"
namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

LoginServlet::LoginServlet()
    :Servlet("LoginServlet") {
}

enum LoginResult {
    Success = 0,
    OtherError = 1,
    PasswordError = 2,
    UserNotExists = 3,
    UserDisabled = 4,
};

int32_t LoginServlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        // FIBER_LOG_INFO(g_logger) << "login request body: " << body;

        Json::Value json;
        FIBER_ASSERT(Fiber::GetThis()->getState() == Fiber::State::EXEC);
        if (!JsonUtil::FromString(json, body)) {
            FIBER_LOG_ERROR(g_logger) << "json parse error, body: " << body;
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"json parse error\"}", OtherError));
            return -1;
        }

        std::string username = JsonUtil::GetString(json, "user");
        std::string password = JsonUtil::GetString(json, "pwd");

        if (username.empty() || password.empty()) {
            FIBER_LOG_ERROR(g_logger) << "username or password empty";
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, password is required\"}", OtherError));
            return -1;
        }

#ifdef FIBERSERVER_USE_SOCI
        SociDB::ptr mysql = SociMgr::GetInstance()->get("user_info");
#else
        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("user_info");
#endif
        if (!mysql) {
            FIBER_LOG_ERROR(g_logger) << "mysql connection error";
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }
          FIBER_ASSERT(Fiber::GetThis()->getState() == Fiber::State::EXEC);
        auto user = user_info::GetUserByUsername(mysql, username);
        if (!user) {
            FIBER_LOG_INFO(g_logger) << "user not exists: " << username;
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"user not exists\"}", UserNotExists));
            return 0;
        }
          FIBER_ASSERT(Fiber::GetThis()->getState() == Fiber::State::EXEC);
        if (user->status != 1) {
            FIBER_LOG_INFO(g_logger) << "user disabled: " << username;
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"user is disabled\"}", UserDisabled));
            return 0;
        }

        std::string password_hash = md5(password + user->salt);
        if (password_hash != user->password) {
            FIBER_LOG_INFO(g_logger) << "password error: " << username;
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"password error\"}", PasswordError));
            return 0;
        }

        // user_info::UpdateLastLogin(mysql, user->id);
          FIBER_ASSERT(Fiber::GetThis()->getState() == Fiber::State::EXEC);
        // FIBER_LOG_INFO(g_logger) << "user login success: " << username;

        Json::Value result;
        result["code"] = Success;
        result["msg"] = "login success";
        result["nickname"] = user->nickname;
        result["user_id"] = user->id;
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
