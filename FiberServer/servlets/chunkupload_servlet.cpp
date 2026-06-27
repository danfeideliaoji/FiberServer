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
    ScopedPerfLog perf(request->getPath());
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string file_path = request->getHeader("X-File-Path");
    std::string body = request->getBody();
    try {
        // 分片上传兼容两种来源：body 直接传分片，或 Nginx 落盘后通过 X-File-Path 传临时文件路径。
        // 元数据优先从 query 读取；Nginx/客户端也可以把缺失字段放进 X-Chunk-Meta。
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
        {
            // total_chunks 和 chunk_index 是分片协议字段，必须在保存分片前确定。
            total_chunks = request->getParamAs<int>("total_chunks", 0);
            chunk_index = request->getParamAs<int>("chunk_index", -1);
        }
        // X-Chunk-Meta 用来补齐 artifact_name/version/build_no/branch/commit_id 等制品元数据。
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
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"project_name, checksum, size, chunk_index, artifact_type, total_chunks required\"}", OtherError));
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

        if(request_meta.artifact_mode) {
            // 分片写入前先检查坐标，避免冲突制品留下临时分片或触发最终合并上传。
            struct CoordinateCheckResult {
                int code = Success;
                bool stop = false;
                std::string message;
            };
            auto coordinate_check = DbExecutorMgr::GetInstance()->submit([request_meta]() {
                CoordinateCheckResult result;
                SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
                if(!mysql) {
                    result.code = OtherError;
                    result.stop = true;
                    result.message = "mysql connection error";
                    return result;
                }
                auto artifact = artifact_info::GetArtifact(mysql, request_meta.owner,
                                                           request_meta.version,
                                                           request_meta.build_no,
                                                           request_meta.artifact_name);
                if(!artifact) {
                    return result;
                }
                result.stop = true;
                if(artifact->checksum == request_meta.checksum) {
                    result.message = "artifact already exists";
                } else {
                    result.code = OtherError;
                    result.message = "artifact checksum conflict";
                }
                return result;
            });
            if(coordinate_check.stop) {
                response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"%s\"}",
                    coordinate_check.code, coordinate_check.message.c_str()));
                return coordinate_check.code == Success ? 0 : -1;
            }
        }

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
        // 所有分片到齐后先按编号合并本地临时文件，再上传到 FastDFS。
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
            // 合并上传完成后，数据库三类记录必须同事务提交，保证引用计数和制品元数据一致。
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
