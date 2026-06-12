#include "chunkupload_servlet.h"
#include "FiberServer/my/chunkManager.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include <exception>
namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");
ChunkUploadServlet::ChunkUploadServlet()
    :Servlet("ChunkUploadServlet") {
}

enum ChunkUploadCode{
    Success = 0,
    OtherError=1,
    AllChunksUploaded=2,
};
int32_t ChunkUploadServlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string file_path = request->getHeader("X-File-Path");
    std::string body = request->getBody();
    try {
        if(file_path.empty()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"X-File-Path is required\"}", OtherError));
            return -1;
        }

        // 元数据从 body JSON 获取（Nginx 仍然会转发 body）
        // 但如果 Nginx client_body_in_file_only=on，body 可能为空
        // 所以也支持从 query params 获取
        Json::Value json;
        std::string username, md5, type, filename;
        int64_t size = 0;
        int total_chunks = 0, chunk_index = -1;
        // FIBER_LOG_INFO(g_logger) << "body: " << body;
        // if (!body.empty() && JsonUtil::FromString(json, body)) {
        //     FIBER_LOG_INFO(g_logger) << "get param from body";
        //     username = JsonUtil::GetString(json, "username");
        //     md5 = JsonUtil::GetString(json, "md5");
        //     size = JsonUtil::GetInt64(json, "size");
        //     type = JsonUtil::GetString(json, "type");
        //     filename = JsonUtil::GetString(json, "filename");
        //     total_chunks = JsonUtil::GetInt32(json, "total_chunks", 0);
        //     chunk_index = JsonUtil::GetInt32(json, "chunk_index", -1);
        // } else
        // FIBER_LOG_INFO(g_logger) << "body size: " << body.size();
        {
            // 从 query params 获取（前端可通过 URL 参数传递）
            // FIBER_LOG_INFO(g_logger) << "get param from query";
            username = request->getParam("username");
            md5 = request->getParam("md5");
            size = request->getParamAs<int64_t>("size", 0);
            type = request->getParam("type");
            filename = request->getParam("filename");
            total_chunks = request->getParamAs<int>("total_chunks", 0);
            chunk_index = request->getParamAs<int>("chunk_index", -1);
        }
        // FIBER_LOG_INFO(g_logger) << "username: " << username << " md5: " << md5 << " size: " << size << " type: " << type << " filename: " << filename << " total_chunks: " << total_chunks << " chunk_index: " << chunk_index;
        // 也尝试从自定义 header 获取（最可靠的方式）
        std::string meta_header = request->getHeader("X-Chunk-Meta");
        if (!meta_header.empty()) {
            FIBER_LOG_INFO(g_logger) << "get param from header";
            Json::Value meta;
            if (JsonUtil::FromString(meta, meta_header)) {
                if (username.empty()) username = JsonUtil::GetString(meta, "username");
                if (md5.empty()) md5 = JsonUtil::GetString(meta, "md5");
                if (size <= 0) size = JsonUtil::GetInt64(meta, "size");
                if (type.empty()) type = JsonUtil::GetString(meta, "type");
                if (total_chunks <= 0) total_chunks = JsonUtil::GetInt32(meta, "total_chunks", 0);
                if (chunk_index < 0) chunk_index = JsonUtil::GetInt32(meta, "chunk_index", -1);
            }
        }

        if(username.empty() || md5.empty() || size <= 0 || chunk_index < 0 || type.empty() || total_chunks <= 0) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, md5, size, chunk_index, type, total_chunks required\"}", OtherError));
            return -1;
        }

        // FIBER_LOG_INFO(g_logger) << "chunk upload: user=" << username << " md5=" << md5
        //                          << " chunk=" << chunk_index+1 << "/" << total_chunks;

        if(!ChunkManager::saveChunk(file_path, username, md5, chunk_index)) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"save chunk failed\"}", OtherError));
            return -1;
        }
        if(!ChunkManager::isAllChunksReady(username, md5, total_chunks)) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"chunk %d/%d uploaded\"}", Success, chunk_index + 1, total_chunks));
            return 0;
        }
        // FIBER_LOG_INFO(g_logger) << "all chunks uploaded";
        // 所有分片就绪，合并并上传到 FastDFS
        std::string merged_path = ChunkManager::mergeChunks(username, md5, total_chunks);
        if (merged_path.empty()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"merge chunks failed\"}", OtherError));
            return -1;
        }

        std::string file_id;
        if (!FastDFSUtil::uploadBigFile(merged_path, file_id)) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"upload to fastdfs failed\"}", OtherError));
            return -1;
        }

        MySQL::ptr mysql = MySQLMgr::GetInstance()->get("file_info");
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
        MySQL::ptr shared_db = MySQLMgr::GetInstance()->get("file_shared");
        if(shared_db) {
            file_shared::CreateShared(shared_db, md5, file_id, size);
        }

        std::string path = ChunkManager::buildTaskPath(username, md5);
        FSUtil::Rm(path);
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", AllChunksUploaded));
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}
}
}
