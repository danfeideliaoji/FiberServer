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
    m_dispatch->addServlet("/api/status", Servlet::ptr(new StatusServlet));
    m_dispatch->addServlet("/api/artifacts/precheck", Servlet::ptr(new UploadServlet));
    m_dispatch->addServlet("/api/artifacts/upload/direct", Servlet::ptr(new DirUploadServlet));
    m_dispatch->addServlet("/api/artifacts/upload/chunk", Servlet::ptr(new ChunkUploadServlet));
    m_dispatch->addServlet("/api/artifacts/list", Servlet::ptr(new MyFilesServlet));
    m_dispatch->addServlet("/api/artifacts/checksum", Servlet::ptr(new Md5Servlet));
    m_dispatch->addGlobServlet("/api/artifacts/download", Servlet::ptr(new DownloadServlet));
    m_dispatch->addServlet("/api/artifacts/delete", Servlet::ptr(new DeleteFileServlet));
    m_dispatch->addServlet("/api/artifacts/token", Servlet::ptr(new ArtifactTokenServlet));
    m_dispatch->addServlet("/api/artifacts/latest", Servlet::ptr(new ArtifactQueryServlet));
    m_dispatch->addServlet("/api/artifacts/versions", Servlet::ptr(new ArtifactQueryServlet));
    m_dispatch->addServlet("/api/artifacts/builds", Servlet::ptr(new ArtifactQueryServlet));
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
