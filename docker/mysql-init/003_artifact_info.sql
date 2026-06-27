USE FiberServer;

CREATE TABLE IF NOT EXISTS artifact_info (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    project_name VARCHAR(128) NOT NULL,
    version VARCHAR(128) NOT NULL DEFAULT '',
    build_no VARCHAR(128) NOT NULL DEFAULT '',
    artifact_name VARCHAR(256) NOT NULL,
    checksum VARCHAR(128) NOT NULL,
    file_id VARCHAR(512) NOT NULL,
    size BIGINT NOT NULL DEFAULT 0,
    artifact_type VARCHAR(128) NOT NULL DEFAULT '',
    branch VARCHAR(128) NOT NULL DEFAULT '',
    commit_id VARCHAR(128) NOT NULL DEFAULT '',
    create_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_project_version_build_name (project_name, version, build_no, artifact_name),
    INDEX idx_project_id (project_name, id),
    INDEX idx_project_version (project_name, version),
    INDEX idx_checksum (checksum),
    INDEX idx_file_id (file_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
