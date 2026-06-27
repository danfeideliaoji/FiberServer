#pragma once

#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class ArtifactQueryServlet : public Servlet {
public:
    ArtifactQueryServlet();
    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;
};

}
}
