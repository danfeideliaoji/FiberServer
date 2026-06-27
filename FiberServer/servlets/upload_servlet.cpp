#include "upload_servlet.h"
#include "FiberServer/servlets/artifact_auth.h"
#include <string>
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/my/chunkManager.h"
#include "FiberServer/util/perf_util.h"
#include <exception>
namespace FiberServer {
namespace http {
static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");
UploadServlet::UploadServlet()
    :Servlet("UploadServlet") {
}
enum UploadCode{
    Success = 0,
    direct = 1,
    chunk = 2,
    OtherError = 3,
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

int32_t UploadServlet::handle(http::HttpRequest::ptr request
               , http::HttpResponse::ptr response
               , http::HttpSession::ptr session) {
    ScopedPerfLog perf("/api/upload");
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;

        if (!JsonUtil::FromString(json, body) || !json.isObject()) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"json parse error\"}", OtherError));
            return -1;
        }
        auto meta = ArtifactMetadata::FromJson(json);
        std::string username = meta.owner;
        std::string md5 = meta.checksum;
        std::string filename = meta.storage_name;
        int64_t size = meta.size;
        if(username.empty() || md5.empty() || size <= 0) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"project_name/username, checksum/md5, size required\"}", OtherError));
            return -1;
        }
        if(!RequireArtifactToken(request, meta, response)) {
            return -1;
        }
        PerfTimer db_timer;
        struct DbResult {
            int code = direct;
            std::string status;
            std::string message;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([username, md5, filename, size, meta]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if(!mysql){
                result.code = OtherError;
                result.message = "mysql connection error";
                return result;
            }
            if(meta.artifact_mode) {
                auto artifact = artifact_info::GetArtifact(mysql, meta.owner, meta.version,
                                                           meta.build_no, meta.artifact_name);
                if(artifact) {
                    result.code = Success;
                    result.status = "artifact_existing";
                    result.message = "artifact already exists";
                    return result;
                }
            }
            if(file_info::ExistsByMd5AndUser(mysql, md5, username)){
                if(meta.artifact_mode) {
                    auto file_id = file_shared::GetFileIdByMd5(mysql, md5);
                    if(file_id.empty() ||
                       !artifact_info::CreateArtifact(mysql, BuildArtifactInfo(meta, file_id))) {
                        result.code = OtherError;
                        result.message = "create artifact metadata failed";
                        return result;
                    }
                }
                result.code = Success;
                result.status = "instant_existing";
                result.message = "instant upload success";
                return result;
            }
            if(file_shared::ExistsByMd5(mysql, md5)){
                std::string file_id = file_shared::GetFileIdByMd5(mysql, md5);
                soci::transaction tr(mysql->session());
                if(!file_shared::IncrementRef(mysql, md5) ||
                   !file_info::CreateFile(mysql, md5, file_id, username, filename, size, "")) {
                    tr.rollback();
                    result.code = OtherError;
                    result.message = "instant upload db transaction failed";
                    return result;
                }
                if(meta.artifact_mode &&
                   !artifact_info::CreateArtifact(mysql, BuildArtifactInfo(meta, file_id))) {
                    tr.rollback();
                    result.code = OtherError;
                    result.message = "create artifact metadata failed";
                    return result;
                }
                tr.commit();
                result.code = Success;
                result.status = "instant_shared";
                result.message = "instant upload success";
            }
            return result;
        });
        perf.addDbMs(db_timer.elapsedMs());
        if (db_result.code == Success || db_result.code == OtherError) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"%s\"}",
                db_result.code, db_result.message.c_str()));
            if (!db_result.status.empty()) {
                perf.setStatus(db_result.status);
            }
            return db_result.code == Success ? 0 : -1;
        }

        PerfTimer file_io_timer;
        std::vector<int> uploadedChunks = ChunkManager::getUploadedChunks(username, md5);
        perf.addFileIoMs(file_io_timer.elapsedMs());
        
        // 闁哄秷顫夊畵渚€寮崶锔筋偨濠㈠爢鍐瘓闁告劕鍟块悾鐐▔婵犱胶鐐婃俊顖椻偓宕囩
        if (size <= ChunkManager::getChunkSizes()) {
            FIBER_LOG_INFO(g_logger) << "direct upload, size=" << size;
            response->setBody(StringUtil::Format(
                "{\"code\":%d,\"msg\":\"direct upload\",\"uploadedChunks\":[]}", direct));
            perf.setStatus("direct");
            return 0;
        }

        int totalChunks = (int)((size + ChunkManager::getChunkSizes() - 1) / ChunkManager::getChunkSizes());

        PerfTimer mkdir_timer;
        FSUtil::Mkdir(ChunkManager::buildTaskPath(username, md5));
        perf.addFileIoMs(mkdir_timer.elapsedMs());

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
        perf.setStatus("chunk");
        return 0;
    } catch (std::exception& e) {
        FIBER_LOG_ERROR(g_logger) << "handle exception: " << e.what();
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"internal error\"}", OtherError));
        return -1;
    }
}}
}
