#include "register_servlet.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/util/hash_util.h"
#include "FiberServer/util/perf_util.h"
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
    ScopedPerfLog perf("/api/register");
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

        PerfTimer db_timer;
        std::string salt = random_string(16);
        std::string password_hash = md5(password + salt);

        struct DbResult {
            bool db_ok = false;
            bool username_exists = false;
            bool create_ok = false;
            int64_t user_id = 0;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit(
            [username, password_hash, salt, nickname]() {
                DbResult result;
                SociDB::ptr mysql = SociMgr::GetInstance()->get("user_info");
                if (!mysql) {
                    return result;
                }
                result.db_ok = true;
                if (user_info::GetUserByUsername(mysql, username)) {
                    result.username_exists = true;
                    return result;
                }
                result.create_ok = user_info::CreateUser(
                    mysql, username, password_hash, salt, nickname, result.user_id);
                return result;
            });

        if (!db_result.db_ok) {
            perf.addDbMs(db_timer.elapsedMs());
            FIBER_LOG_ERROR(g_logger) << "mysql connection error";
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }

        if (db_result.username_exists) {
            perf.addDbMs(db_timer.elapsedMs());
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"username already exists\"}", UsernameExists));
            perf.setStatus("username_exists");
            return 0;
        }

        if (!db_result.create_ok) {
            perf.addDbMs(db_timer.elapsedMs());
            FIBER_LOG_ERROR(g_logger) << "create user failed";
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"create user failed\"}", OtherError));
            return -1;
        }
        perf.addDbMs(db_timer.elapsedMs());

        FIBER_LOG_INFO(g_logger) << "register success, user_id: " << db_result.user_id;
        Json::Value result;
        result["code"] = Success;
        result["msg"] = "register success";
        result["user_id"] = db_result.user_id;
        response->setBody(JsonUtil::ToString(result));
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
