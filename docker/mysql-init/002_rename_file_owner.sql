USE FiberServer;

SET @has_url := (
    SELECT COUNT(*)
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'file_info'
      AND COLUMN_NAME = 'url'
);

SET @sql := IF(
    @has_url > 0,
    'ALTER TABLE file_info CHANGE COLUMN url owner VARCHAR(256) NOT NULL DEFAULT ''''',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @has_old_index := (
    SELECT COUNT(*)
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'file_info'
      AND INDEX_NAME = 'idx_user_filename'
);

SET @sql := IF(
    @has_old_index > 0,
    'ALTER TABLE file_info DROP INDEX idx_user_filename',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @has_owner_index := (
    SELECT COUNT(*)
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'file_info'
      AND INDEX_NAME = 'idx_owner_filename'
);

SET @sql := IF(
    @has_owner_index = 0,
    'CREATE INDEX idx_owner_filename ON file_info(owner, filename)',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @has_owner_id_index := (
    SELECT COUNT(*)
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'file_info'
      AND INDEX_NAME = 'idx_owner_id'
);

SET @sql := IF(
    @has_owner_id_index = 0,
    'CREATE INDEX idx_owner_id ON file_info(owner, id)',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @has_owner_md5_index := (
    SELECT COUNT(*)
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'file_info'
      AND INDEX_NAME = 'idx_owner_md5'
);

SET @sql := IF(
    @has_owner_md5_index = 0,
    'CREATE INDEX idx_owner_md5 ON file_info(owner, md5)',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
