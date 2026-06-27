CREATE DATABASE IF NOT EXISTS FiberServer DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE FiberServer;

CREATE TABLE IF NOT EXISTS user_info (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(128) NOT NULL UNIQUE,
    password VARCHAR(256) NOT NULL,
    salt VARCHAR(128) NOT NULL,
    nickname VARCHAR(128) NOT NULL DEFAULT '',
    status TINYINT NOT NULL DEFAULT 1,
    last_login TIMESTAMP NULL DEFAULT NULL,
    create_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS file_info (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    md5 VARCHAR(128) NOT NULL,
    file_id VARCHAR(512) NOT NULL,
    owner VARCHAR(256) NOT NULL DEFAULT '',
    filename VARCHAR(256) NOT NULL DEFAULT '',
    size BIGINT NOT NULL DEFAULT 0,
    type VARCHAR(128) NOT NULL DEFAULT '',
    count INT NOT NULL DEFAULT 1,
    create_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_md5 (md5),
    INDEX idx_file_id (file_id),
    INDEX idx_owner_filename (owner, filename),
    INDEX idx_owner_id (owner, id),
    INDEX idx_owner_md5 (owner, md5)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS file_shared (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    file_md5 VARCHAR(128) NOT NULL UNIQUE,
    file_id VARCHAR(512) NOT NULL,
    file_size BIGINT NOT NULL DEFAULT 0,
    ref_count INT NOT NULL DEFAULT 1,
    create_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    update_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_file_id (file_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

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
