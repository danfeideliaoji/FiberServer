#pragma once

#include "FiberServer/my/artifact_metadata.h"
#include "FiberServer/net/http/http.h"

namespace FiberServer {
namespace http {

std::string GetArtifactToken(const HttpRequest::ptr& request);
bool RequireArtifactToken(const HttpRequest::ptr& request,
                          const ArtifactMetadata& meta,
                          const HttpResponse::ptr& response);

}
}
