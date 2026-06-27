#include "deletefile_servlet.h"
#include "FiberServer/servlets/artifact_auth.h"
#include "FiberServer/base/util.h"
#include "FiberServer/base/log.h"
#include "FiberServer/db/db_executor.h"
#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/my/fastdfs.h"
#include "FiberServer/util/perf_util.h"
#include <exception>
namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("Servlet");

enum DeleteFileCode{
    Success = 0,
    OtherError = 1,
};

DeleteFileServlet::DeleteFileServlet()
    :Servlet("DeleteFileServlet") {
}
int32_t DeleteFileServlet::handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session)  {
    ScopedPerfLog perf(request->getPath());
    response->setHeader("Content-Type", "text/json charset=utf-8");
    std::string body = request->getBody();
    try {
        Json::Value json;
        if(!JsonUtil::FromString(json, body)){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"parse body failed\"}", OtherError));
            return -1;
        }
        auto meta = ArtifactMetadata::FromJson(json);
        std::string username = meta.owner;
        std::string file_name = meta.storage_name;
        if(username.empty() || file_name.empty()){
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"project_name and artifact_name are required\"}", OtherError));
            return -1;
        }
        if(!RequireArtifactToken(request, meta, response)) {
            return -1;
        }
        PerfTimer db_timer;
        struct DbResult {
            bool ok = false;
            bool not_found = false;
            bool delete_physical = false;
            std::string file_id;
            std::string message;
        };
        auto db_result = DbExecutorMgr::GetInstance()->submit([username, file_name, meta]() {
            DbResult result;
            SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
            if(!mysql){
                result.message = "mysql connection error";
                return result;
            }
            // artifact 删除按制品坐标定位，先删除 artifact_info，再处理内部 file_info 逻辑记录。
            auto artifact = artifact_info::GetArtifact(mysql, meta.owner, meta.version,
                                                       meta.build_no, meta.artifact_name);
            if(!artifact) {
                result.not_found = true;
                result.message = "artifact not found";
                return result;
            }
            soci::transaction tr(mysql->session());
            if(!artifact_info::DeleteArtifact(mysql, meta.owner, meta.version,
                                              meta.build_no, meta.artifact_name)) {
                tr.rollback();
                result.message = "delete artifact metadata failed";
                return result;
            }
            if(!file_info::DeleteFileRecordByUserAndFilename(mysql, username, file_name)){
                tr.rollback();
                result.message = "delete file record failed";
                return result;
            }
            // 物理文件可能被多个项目共享，只有引用计数归零才删除 shared 记录和 FastDFS 文件。
            int ref = file_shared::DecrementRef(mysql, artifact->checksum);
            if(ref < 0) {
                tr.rollback();
                result.message = "decrement shared ref failed";
                return result;
            }
            if(ref == 0 && !file_shared::DeleteShared(mysql, artifact->checksum)) {
                tr.rollback();
                result.message = "delete shared record failed";
                return result;
            }
            tr.commit();
            result.ok = true;
            result.delete_physical = ref == 0;
            result.file_id = artifact->file_id;
            return result;
        });
        perf.addDbMs(db_timer.elapsedMs());
        if(!db_result.ok){
            if (db_result.not_found) {
                perf.setStatus("not_found");
            }
            response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"%s\"}", OtherError, db_result.message.c_str()));
            return -1;
        }
        if(db_result.delete_physical){
            PerfTimer fastdfs_timer;
            FastDFSUtil::deleteFile(db_result.file_id);
            perf.addFastDfsMs(fastdfs_timer.elapsedMs());
        }
        response->setBody(StringUtil::Format("{\"code\":%d,\"msg\":\"success\"}", Success));
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
