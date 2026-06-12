#pragma once
#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class DirUploadServlet : public Servlet {
public:
    DirUploadServlet();
    virtual int32_t handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session) ;
};
}
}