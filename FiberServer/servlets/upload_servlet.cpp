#include "upload_servlet.h"
#include <string>
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/my/chunkManager.h"
#include <exception>
namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");
UploadServlet::UploadServlet()
    :Servlet("UploadServlet") {
}
enum UploadCode{
    Success = 0,    // 秒传成功（文件已存在）
    direct = 1,     // 直传模式
    chunk = 2,      // 分片模式
    OtherError = 3,
};
int32_t UploadServlet::handle(http::HttpRequest::ptr request
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
        int64_t size = JsonUtil::GetInt64(json, "size");
        if(username.empty() || md5.empty() || size <= 0) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, md5, size required\"}", OtherError));
            return -1;
        }
        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
        MySQL::ptr shared_db = MySQLMgr::GetInstance()->get("file_shared");
        if(!mysql || !shared_db){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"mysql connection error\"}", OtherError));
            return -1;
        }

        // 秒传检查：该用户已有此文件
        if(file_info::ExistsByMd5AndUser(mysql, md5, username)){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"instant upload success\"}", Success));
            return 0;
        }

        // 共享文件表中存在此md5，秒传：增加引用计数，为用户创建记录
        if(file_shared::ExistsByMd5(shared_db, md5)){
            FIBER_LOG_INFO(g_logger) << "instant upload success, shared file";
            std::string file_id = file_shared::GetFileIdByMd5(shared_db, md5);
            file_shared::IncrementRef(shared_db, md5);
            file_info::CreateFile(mysql, md5, file_id, username, filename, size, "");
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"instant upload success\"}", Success));
            return 0;
        }
        
        // 获取已上传的分片（断点续传用）
        std::vector<int> uploadedChunks = ChunkManager::getUploadedChunks(username, md5);
        
        // 根据文件大小决定上传模式
        if (size <= ChunkManager::getChunkSizes()) {
            FIBER_LOG_INFO(g_logger) << "direct upload, size=" << size;
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"direct upload\",\"uploadedChunks\":[]}", direct));
            return 0;
        }

        int totalChunks = (int)((size + ChunkManager::getChunkSizes() - 1) / ChunkManager::getChunkSizes());

        FSUtil::Mkdir(ChunkManager::buildTaskPath(username, md5));

        FIBER_LOG_INFO(g_logger) << "chunk upload, totalChunks=" << totalChunks 
                                 << ", uploaded=" << uploadedChunks.size();
        std::string chunksJson = "[";
        for (size_t i = 0; i < uploadedChunks.size(); ++i) {
            chunksJson += std::to_string(uploadedChunks[i]);
            if (i < uploadedChunks.size() - 1) chunksJson += ",";
        }
        chunksJson += "]";
        response->setBody(StringUtil::Format(
            "{\"code\":%d,\"msg\":\"chunk upload\",\"totalChunks\":%d,\"uploadedChunks\":%s}",
            chunk, totalChunks, chunksJson.c_str()));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}}
}
