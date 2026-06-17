#include "dirupload_servlet.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"
#include "FiberServer/util/hash_util.h"
#include "FiberServer/util/perf_util.h"
namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");
DirUploadServlet::DirUploadServlet()
    :Servlet("DirUploadServlet") {
}

enum DirUploadCode{
    Success = 0,
    OtherError=1,
};
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

int32_t DirUploadServlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    ScopedPerfLog perf("/api/upload/dirupload");
    response->setHeader("Content-Type", "text/json charset=utf-8");

    auto params = parseQuery(request->getQuery());
    std::string username = params["username"];
    std::string md5 = params["md5"];
    std::string filename = params["filename"];
    // FIBER_LOG_INFO(g_logger)<<"accept filename"<<filename;
    int64_t size = atoll(params["size"].c_str());
    std::string type = params["type"];
    if(username.empty() || md5.empty() || size <= 0 || type.empty()) {
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, md5, size, type required\"}", OtherError));
        return -1;
    }
    
    std::string fileContent = request->getBody();
    if(fileContent.empty()) {
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"empty file content\"}", OtherError));
        return -1;
    }
    
    std::string file_id;
    FIBER_LOG_INFO(g_logger) << "direct upload: user=" << username << " md5=" << md5 << " size=" << fileContent.size();
    PerfTimer fastdfs_timer;
    if(!FastDFSUtil::uploadSmallFile(fileContent, file_id)){
        perf.addFastDfsMs(fastdfs_timer.elapsedMs());
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"upload file failed\"}", OtherError));
        return -1;
    }
    perf.addFastDfsMs(fastdfs_timer.elapsedMs());

    PerfTimer db_timer;
    struct DbResult {
        bool ok = false;
        std::string message;
    };
    auto db_result = DbExecutorMgr::GetInstance()->submit([md5, file_id, username, filename, size, type]() {
        DbResult result;
        SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
        if(!mysql){
            result.message = "mysql connection error";
            return result;
        }
        soci::transaction tr(mysql->session());
        if(!file_info::CreateFile(mysql, md5, file_id, username, filename, size, type)){
            tr.rollback();
            result.message = "create file record failed";
            return result;
        }
        if(!file_shared::CreateShared(mysql, md5, file_id, size)) {
            tr.rollback();
            result.message = "create shared file record failed";
            return result;
        }
        tr.commit();
        result.ok = true;
        return result;
    });
    perf.addDbMs(db_timer.elapsedMs());
    if(!db_result.ok){
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"%s\"}", OtherError, db_result.message.c_str()));
        return -1;
    }
    response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", Success));
    perf.setStatus("ok");
    return 0;
}
}
}
