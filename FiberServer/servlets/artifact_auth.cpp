#include "artifact_auth.h"

#include "FiberServer/my/mysqlop.h"

namespace FiberServer {
namespace http {

static bool IsArtifactWritePath(const std::string& path) {
    return path == "/api/artifacts/precheck" ||
           path == "/api/artifacts/upload/direct" ||
           path == "/api/artifacts/upload/chunk" ||
           path == "/api/artifacts/delete";
}

std::string GetArtifactToken(const HttpRequest::ptr& request) {
    auto authorization = request->getHeader("authorization");
    const std::string prefix = "Bearer ";
    if (authorization.size() > prefix.size() &&
        authorization.compare(0, prefix.size(), prefix) == 0) {
        return authorization.substr(prefix.size());
    }

    auto token = request->getHeader("X-Artifact-Token");
    if (!token.empty()) {
        return token;
    }
    return request->getParam("token");
}

bool RequireArtifactToken(const HttpRequest::ptr& request,
                          const ArtifactMetadata& meta,
                          const HttpResponse::ptr& response) {
    if (!IsArtifactWritePath(request->getPath())) {
        return true;
    }

    auto token = GetArtifactToken(request);
    if (token.empty()) {
        response->setBody("{\"code\":1,\"msg\":\"artifact token required\"}");
        return false;
    }

    SociDB::ptr mysql = SociMgr::GetInstance()->get("file_info");
    if (!mysql) {
        response->setBody("{\"code\":1,\"msg\":\"mysql connection error\"}");
        return false;
    }
    if (!project_token::ValidateToken(mysql, meta.owner, token)) {
        response->setBody("{\"code\":1,\"msg\":\"invalid artifact token\"}");
        return false;
    }
    return true;
}

}
}
