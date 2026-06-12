#pragma once
#include "FiberServer/net/http/servlet.h"

namespace FiberServer {
namespace http {

class Md5Servlet : public Servlet {
public:
    Md5Servlet();
    virtual int32_t handle(http::HttpRequest::ptr request
                          , http::HttpResponse::ptr response
                          , http::HttpSession::ptr session) override;
};

}
}
