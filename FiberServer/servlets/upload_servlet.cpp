#include "upload_servlet.h"
#include <string>
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"
#include "FiberServer/db/db_executor.h"
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
        std::string username = JsonUtil::GetString(json, "username");
        std::string md5 = JsonUtil::GetString(json, "md5");
        std::string filename = JsonUtil::GetString(json, "filename");
        int64_t size = JsonUtil::GetInt64(json, "size");
        if(username.empty() || md5.empty() || size <= 0) {
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"username, md5, size required\"}", OtherError));
            return -1;
        }
        PerfTimer db_timer;
        struct DbResult {
            int code = direct;
            std::string status;
            std::string message;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([username, md5, filename, size]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if(!mysql){
                result.code = OtherError;
                result.message = "mysql connection error";
                return result;
            }
            if(file_info::ExistsByMd5AndUser(mysql, md5, username)){
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
