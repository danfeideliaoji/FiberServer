#pragma once
#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class UploadServlet : public Servlet {
public:
    UploadServlet();
    virtual int32_t handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session) override;
};
}
}