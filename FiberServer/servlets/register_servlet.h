#pragma once
#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class RegisterServlet : public Servlet {
public:
    RegisterServlet();
    virtual int32_t handle(http::HttpRequest::ptr request
                   , http::HttpResponse::ptr response
                   , http::HttpSession::ptr session) override;
};
}
}