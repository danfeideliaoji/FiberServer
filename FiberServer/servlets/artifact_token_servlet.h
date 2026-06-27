#pragma once

#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class ArtifactTokenServlet : public Servlet {
public:
    ArtifactTokenServlet();
    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;
};

}
}
