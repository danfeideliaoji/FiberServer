#include "FiberServer/my/artifact_metadata.h"

#include <cassert>
#include <iostream>

int main() {
    auto artifact = FiberServer::ArtifactMetadata::FromFields(
        "auth-service",
        "abc123",
        "auth-service.tar.gz",
        4096,
        "application/gzip",
        "1.2.0",
        "104");

    assert(artifact.artifact_mode);
    assert(artifact.owner == "auth-service");
    assert(artifact.checksum == "abc123");
    assert(artifact.artifact_name == "auth-service.tar.gz");
    assert(artifact.storage_name == "1.2.0/104/auth-service.tar.gz");
    assert(artifact.type == "application/gzip");
    assert(artifact.size == 4096);

    auto legacy = FiberServer::ArtifactMetadata::FromFields(
        "alice",
        "def456",
        "sample.txt",
        12,
        "text/plain",
        "",
        "");

    assert(!legacy.artifact_mode);
    assert(legacy.owner == "alice");
    assert(legacy.checksum == "def456");
    assert(legacy.artifact_name == "sample.txt");
    assert(legacy.storage_name == "sample.txt");
    assert(legacy.type == "text/plain");
    assert(legacy.size == 12);

    std::cout << "artifact metadata contract passed" << std::endl;
    return 0;
}
