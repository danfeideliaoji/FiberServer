#include "dirupload_servlet.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/log.h"
#include "FiberServer/util/hash_util.h"
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
    if(!FastDFSUtil::uploadSmallFile(fileContent, file_id)){
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"upload file failed\"}", OtherError));
        return -1;
    }

#ifdef FIBERSERVER_USE_SOCI
    SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
#else
    MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
#endif
    if(!mysql){
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
        return -1;
    }
    // url 字段存储用户名
    if(!file_info::CreateFile(mysql, md5, file_id, username, filename, size, type)){
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"create file record failed\"}", OtherError));
        return -1;
    }
    // 写入共享文件表
#ifdef FIBERSERVER_USE_SOCI
    SociDB::ptr shared_db = SociMgr::GetInstance()->get("file_shared");
#else
    MySQL::ptr shared_db = MySQLMgr::GetInstance()->get("file_shared");
#endif
    if(shared_db) {
        file_shared::CreateShared(shared_db, md5, file_id, size);
    }
    response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", Success));
    return 0;
}
}
}
