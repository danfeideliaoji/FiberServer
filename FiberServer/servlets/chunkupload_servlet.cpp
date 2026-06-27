#include "chunkupload_servlet.h"
#include "FiberServer/servlets/artifact_auth.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/chunkManager.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/util/perf_util.h"
#include <exception>
#include <map>
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

static ArtifactInfo BuildArtifactInfo(const ArtifactMetadata& meta, const std::string& file_id) {
    ArtifactInfo artifact;
    artifact.project_name = meta.owner;
    artifact.version = meta.version;
    artifact.build_no = meta.build_no;
    artifact.artifact_name = meta.artifact_name;
    artifact.checksum = meta.checksum;
    artifact.file_id = file_id;
    artifact.size = meta.size;
    artifact.artifact_type = meta.type;
    artifact.branch = meta.branch;
    artifact.commit_id = meta.commit_id;
    return artifact;
}

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
        std::map<std::string, std::string> query_params;
        for (const auto& key : {"project_name", "project", "namespace", "username", "user",
                                "checksum", "md5", "artifact_name", "filename", "file_name",
                                "artifact_type", "type", "version", "build_no", "size"}) {
            auto value = request->getParam(key);
            if (!value.empty()) {
                query_params[key] = value;
            }
        }
        auto request_meta = ArtifactMetadata::FromParams(query_params);
        std::string username = request_meta.owner;
        std::string md5 = request_meta.checksum;
        std::string type = request_meta.type;
        std::string filename = request_meta.storage_name;
        int64_t size = request_meta.size;
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
                auto header_meta = ArtifactMetadata::FromJson(meta);
                if (username.empty()) username = header_meta.owner;
                if (md5.empty()) md5 = header_meta.checksum;
                if (size <= 0) size = header_meta.size;
                if (type.empty()) type = header_meta.type;
                if (filename.empty()) filename = header_meta.storage_name;
                if (request_meta.artifact_name.empty()) request_meta.artifact_name = header_meta.artifact_name;
                if (request_meta.version.empty()) request_meta.version = header_meta.version;
                if (request_meta.build_no.empty()) request_meta.build_no = header_meta.build_no;
                if (request_meta.branch.empty()) request_meta.branch = header_meta.branch;
                if (request_meta.commit_id.empty()) request_meta.commit_id = header_meta.commit_id;
                request_meta.owner = username;
                request_meta.checksum = md5;
                request_meta.storage_name = filename;
                request_meta.size = size;
                request_meta.type = type;
                request_meta.artifact_mode = request_meta.artifact_mode || header_meta.artifact_mode;
                if (total_chunks <= 0) total_chunks = JsonUtil::GetInt32(meta, "total_chunks", 0);
                if (chunk_index < 0) chunk_index = JsonUtil::GetInt32(meta, "chunk_index", -1);
            }
        }

        if(username.empty() || md5.empty() || size <= 0 || chunk_index < 0 || type.empty() || total_chunks <= 0) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"project_name/username, checksum/md5, size, chunk_index, artifact_type/type, total_chunks required\"}", OtherError));
            return -1;
        }
        request_meta.owner = username;
        request_meta.checksum = md5;
        request_meta.storage_name = filename;
        request_meta.size = size;
        request_meta.type = type;
        if(!RequireArtifactToken(request, request_meta, response)) {
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
        auto db_result = DbExecutorMgr::GetInstance()->submit([md5, file_id, username, filename, size, type, request_meta]() {
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
            if(request_meta.artifact_mode &&
               !artifact_info::CreateArtifact(mysql, BuildArtifactInfo(request_meta, file_id))) {
                tr.rollback();
                result.message = "create artifact metadata failed";
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
