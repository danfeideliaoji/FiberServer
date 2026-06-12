# MySQL C API to SOCI Migration

本文档记录当前项目把 MySQL 底层 C API 业务访问逐步迁移到 SOCI 的状态、验证方式和后续工作。

目标不是立刻删除所有旧 MySQL 封装，而是先让业务路径可以在 `USE_SOCI_DB=ON` 下不直接依赖 `MYSQL_BIND`、`mysql_stmt_*`、结果缓冲区和列下标解析。

## 当前状态

已完成：

- 新增 `USE_SOCI_DB` CMake 开关。
- Docker 开发镜像已安装 SOCI MySQL backend 依赖。
- 新增 `FiberServer/db/soci_db.h` 和 `FiberServer/db/soci_db.cpp`。
- 新增 `FiberServer/my/mysqlop_soci.cpp`，覆盖当前主要文件元数据和共享文件路径。
- `login/register` 已支持 SOCI 路径。
- `upload/md5/myfiles/download/delete/dirupload/chunkupload` 涉及的主要文件元数据路径已支持 SOCI。
- `FastDFS` 里按 md5 查询元数据的便捷函数已支持 `SociDB::ptr`。
- `USE_SOCI_DB=ON` 时，CMake 不再编译 `FiberServer/db/mysql.cpp` 和 `FiberServer/my/mysqlop.cpp`。
- `USE_SOCI_DB=ON` 时，`mysqlop.h` 不再 include `FiberServer/db/mysql.h`。
- `scheduler.cpp` 中的 `MySQLThreadIniter` 只在旧 MySQL 构建下启用。

当前仍保留：

- 旧 MySQL C API 封装文件：`FiberServer/db/mysql.h`、`FiberServer/db/mysql.cpp`。
- 旧业务访问实现：`FiberServer/my/mysqlop.cpp`。
- 默认构建仍是 `USE_SOCI_DB=OFF`，方便回退。

## 已验证

SOCI 构建：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps -e USE_SOCI_DB=ON fiberserver-dev \
  bash -lc 'cmake -S . -B build-soci -DCMAKE_BUILD_TYPE=Debug -DUSE_SOCI_DB="$USE_SOCI_DB" && cmake --build build-soci -j"$(nproc)"'
```

SOCI 测试：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps -e USE_SOCI_DB=ON fiberserver-dev \
  bash -lc './build-soci/test'
```

旧 MySQL 默认路径：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps -e USE_SOCI_DB=OFF fiberserver-dev \
  bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUSE_SOCI_DB="$USE_SOCI_DB" && cmake --build build -j"$(nproc)" && ./build/test'
```

## 下一步

### 1. 跑真实服务级 E2E

目的：确认 SOCI 不只是能编译，而是在 MySQL + FastDFS + Nginx 的完整链路里行为正确。

建议覆盖：

- 注册新用户。
- 重复注册同名用户。
- 正确密码登录。
- 错误密码登录。
- 小文件直传。
- 大文件分片上传。
- `md5` 秒传。
- `/api/myfiles` 文件列表。
- 下载定位是否正确。
- 删除文件后 `file_info` 和 `file_shared.ref_count` 是否正确。
- `ref_count = 0` 时是否删除 FastDFS 文件和 `file_shared` 记录。

预期产出：

- 一份可重复运行的 Docker E2E 脚本。
- 如果已有 `scripts/docker_e2e.sh`，优先补它。

### 2. 给 SOCI 补连接池和超时语义

当前 `SociManager::get()` 每次创建一个新连接，并且 `timeout_ms` 暂未使用。

下一步应补：

- 连接池，优先复用连接。
- `min_conn/max_conn` 或兼容现有配置语义。
- 获取连接超时。
- 连接失效后的重连。
- 清晰的日志：连接创建、获取失败、SQL 异常。

这里可以先用简单自定义池，不急着一次接入复杂抽象。

### 3. 给秒传和删除路径加事务

当前一些路径会同时修改 `file_info` 和 `file_shared`。后续应保证这些操作原子化。

优先处理：

- 秒传：`file_shared::IncrementRef` + `file_info::CreateFile`。
- 删除：`file_info::DeleteFileRecordByUserAndFilename` + `file_shared::DecrementRef/DeleteShared`。
- 分片合并后落库：`file_info::CreateFile` + `file_shared::CreateShared`。

建议新增一个很薄的 SOCI transaction helper，或者在业务 helper 里直接使用 `soci::transaction`。

### 4. 补齐剩余 user_info SOCI helper

当前登录/注册需要的 user_info SOCI 路径已经有了，但旧接口里还有一些管理类函数没有迁移。

待补：

- `GetUserById`
- `UpdatePassword`
- `UpdateLastLogin`
- `UpdateNickname`
- `UpdateStatus`
- `DeleteUser`
- `GetUsersByStatus`
- `GetUserCount`

这些函数迁移完成后，用户相关业务就可以完全脱离旧 MySQL C API。

### 5. 再决定是否删除旧 MySQL C API

不要现在就删除旧封装。建议等下面条件满足后再删：

- SOCI E2E 通过。
- SOCI 压测没有明显阻塞或错误率问题。
- 连接池和事务补齐。
- 默认构建可以切到 `USE_SOCI_DB=ON`。
- 至少保留一个提交点可快速回退。

删除时再处理：

- 移除 `FiberServer/db/mysql.h` 和 `FiberServer/db/mysql.cpp`。
- 移除 `FiberServer/my/mysqlop.cpp`。
- 清理 CMake 里的 `mysqlclient` 直接链接项。
- 清理 `MySQLThreadIniter` 相关代码。

## 风险

- SOCI 是同步库，需要压测确认不会破坏当前协程 worker 的延迟表现。
- 当前 `SociManager` 还没有连接池，真实并发下性能和连接数都需要验证。
- 秒传和删除路径如果不加事务，异常时可能出现 `file_info` 与 `file_shared` 不一致。
- `filename + user` 定位文件的语义需要确认是否允许同名文件；如果允许同名，应改用 `file_id` 或记录 `id`。

## 当前推荐顺序

1. 先补 Docker E2E 脚本。
2. 跑通 SOCI 的 register/login/upload/md5/myfiles/download/delete 全链路。
3. 加 SOCI 连接池。
4. 给秒传和删除路径加事务。
5. 补齐剩余 user_info helper。
6. 再考虑把默认构建切到 SOCI。
