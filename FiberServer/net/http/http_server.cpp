#include "http_server.h"
#include "FiberServer/base/log.h"
#include "FiberServer/servlets/all_servlet.h" 
// #include "servlets/login_page_servlet.h"
// #include "sylar/http/servlets/status_servlet.h"

namespace FiberServer {
namespace http {

static Logger::ptr g_logger = FIBER_LOG_NAME("system");

HttpServer::HttpServer(bool keepalive
               ,IOManager* worker
               ,IOManager* io_worker
               ,IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker)
    ,m_isKeepalive(keepalive) {
    m_dispatch.reset(new ServletDispatch);

    m_type = "http";
    m_dispatch->addServlet("/api/_/config", Servlet::ptr(new ConfigServlet));
    m_dispatch->addServlet("/api/status", Servlet::ptr(new StatusServlet));
    m_dispatch->addServlet("/api/login", Servlet::ptr(new LoginServlet));
    m_dispatch->addServlet("/api/register", Servlet::ptr(new RegisterServlet));
    m_dispatch->addServlet("/api/upload", Servlet::ptr(new UploadServlet));         // 预检：秒传/直传/分片判断
    m_dispatch->addServlet("/api/upload/dirupload", Servlet::ptr(new DirUploadServlet)); // 直传小文件
    m_dispatch->addServlet("/api/uploadchunk", Servlet::ptr(new ChunkUploadServlet));     // 分片上传
    m_dispatch->addServlet("/api/myfiles", Servlet::ptr(new MyFilesServlet));             // 文件列表
    m_dispatch->addServlet("/api/md5", Servlet::ptr(new Md5Servlet));                     // MD5查询
    m_dispatch->addGlobServlet("/api/download", Servlet::ptr(new DownloadServlet));      // 文件下载
    m_dispatch->addServlet("/api/deletefile", Servlet::ptr(new DeleteFileServlet));         // 删除文件
}

void HttpServer::setName(const std::string& v) {
    TcpServer::setName(v);
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

void HttpServer::handleClient(Socket::ptr client) {
    FIBER_LOG_DEBUG(g_logger) << "handleClient " << *client;
    HttpSession::ptr session(new HttpSession(client));
    do {
        auto req = session->recvRequest();
        if(!req) {
            FIBER_LOG_DEBUG(g_logger) << "recv http request fail, errno="
                << errno << " errstr=" << strerror(errno)
                << " cliet:" << *client << " keep_alive=" << m_isKeepalive;
            break;
        }

        HttpResponse::ptr rsp(new HttpResponse(req->getVersion()
                            ,req->isClose() || !m_isKeepalive));
        rsp->setHeader("Server", getName());
        m_dispatch->handle(req, rsp, session);
        session->sendResponse(rsp);

        if(!m_isKeepalive || req->isClose()) {
            break;
        }
    } while(true);
    session->close();
}

}
}
