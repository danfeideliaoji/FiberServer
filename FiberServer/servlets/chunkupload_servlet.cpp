#include "chunkupload_servlet.h"
#include "FiberServer/my/chunkManager.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/util/perf_util.h"
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
    ScopedPerfLog perf("/api/uploadchunk");
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string file_path = request->getHeader("X-File-Path");
    std::string body = request->getBody();
    try {
        // 閸忓啯鏆熼幑顔荤矤 body JSON 閼惧嘲褰囬敍鍦inx 娴犲秶鍔ф导姘虫祮閸?body閿?        // 娴ｅ棗顩ч弸?Nginx client_body_in_file_only=on閿涘異ody 閸欘垵鍏樻稉铏光敄
        // 閹碘偓娴犮儰绡冮弨顖涘瘮娴?query params 閼惧嘲褰?
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
            // 娴?query params 閼惧嘲褰囬敍鍫濆缁旑垰褰查柅姘崇箖 URL 閸欏倹鏆熸导鐘烩偓鎺炵礆
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
        // 娑旂喎鐨剧拠鏇氱矤閼奉亜鐣炬稊?header 閼惧嘲褰囬敍鍫熸付閸欘垶娼惃鍕煙瀵骏绱?
        std::string meta_header = request->getHeader("X-Chunk-Meta");
        if (!meta_header.empty()) {
            FIBER_LOG_INFO(g_logger) << "get param from header";
            Json::Value meta;
            if (JsonUtil::FromString(meta, meta_header)) {
                if (username.empty()) username = JsonUtil::GetString(meta, "username");
                if (md5.empty()) md5 = JsonUtil::GetString(meta, "md5");
                if (size <= 0) size = JsonUtil::GetInt64(meta, "size");
                if (type.empty()) type = JsonUtil::GetString(meta, "type");
                if (filename.empty()) filename = JsonUtil::GetString(meta, "filename");
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

        bool saved = false;
        PerfTimer file_io_timer;
        if(!file_path.empty()) {
            saved = ChunkManager::saveChunk(file_path, username, md5, chunk_index);
        } else {
            saved = ChunkManager::saveChunkContent(body, username, md5, chunk_index);
        }
        perf.addFileIoMs(file_io_timer.elapsedMs());
        if(!saved) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"save chunk failed\"}", OtherError));
            return -1;
        }
        PerfTimer ready_timer;
        if(!ChunkManager::isAllChunksReady(username, md5, total_chunks)) {
            perf.addFileIoMs(ready_timer.elapsedMs());
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"chunk %d/%d uploaded\"}", Success, chunk_index + 1, total_chunks));
            perf.setStatus("chunk_saved");
            return 0;
        }
        perf.addFileIoMs(ready_timer.elapsedMs());
        // FIBER_LOG_INFO(g_logger) << "all chunks uploaded";
        // 閹碘偓閺堝鍨庨悧鍥ф皑缂侇亷绱濋崥鍫濊嫙楠炴湹绗傛导鐘插煂 FastDFS
        PerfTimer merge_timer;
        std::string merged_path = ChunkManager::mergeChunks(username, md5, total_chunks);
        perf.addFileIoMs(merge_timer.elapsedMs());
        if (merged_path.empty()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"merge chunks failed\"}", OtherError));
            return -1;
        }

        std::string file_id;
        PerfTimer fastdfs_timer;
        if (!FastDFSUtil::uploadBigFile(merged_path, file_id)) {
            perf.addFastDfsMs(fastdfs_timer.elapsedMs());
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"upload to fastdfs failed\"}", OtherError));
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

        std::string path = ChunkManager::buildTaskPath(username, md5);
        PerfTimer cleanup_timer;
        FSUtil::Rm(path);
        perf.addFileIoMs(cleanup_timer.elapsedMs());
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", AllChunksUploaded));
        perf.setStatus("merged");
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}
}
}
