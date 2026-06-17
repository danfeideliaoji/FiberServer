# MySQL C API to SOCI Migration

本文档记录当前项目把 MySQL 底层 C API 业务访问迁移到 SOCI 的状态、验证方式和后续工作。

当前目标已经完成：业务路径不再直接依赖 `MYSQL_BIND`、`mysql_stmt_*`、结果缓冲区和列下标解析，默认且唯一的数据库访问后端为 SOCI。

## 当前状态

已完成：

- Docker 开发镜像已安装 SOCI MySQL backend 依赖。
- 新增 `FiberServer/db/soci_db.h` 和 `FiberServer/db/soci_db.cpp`。
- 新增 `FiberServer/my/mysqlop_soci.cpp`，覆盖用户、文件元数据和共享文件路径。
- `login/register` 已支持 SOCI 路径。
- `upload/md5/myfiles/download/delete/dirupload/chunkupload` 涉及的主要文件元数据路径已支持 SOCI。
- 旧 MySQL 版 `user_info` 管理类 helper 已补齐 SOCI 实现：`GetUserById`、`UpdatePassword`、`UpdateLastLogin`、`UpdateNickname`、`UpdateStatus`、`DeleteUser`、`GetUsersByStatus`、`GetUserCount`。
- `FastDFS` 里按 md5 查询元数据的便捷函数已支持 `SociDB::ptr`。
- 已移除旧 MySQL C API 封装文件：`FiberServer/db/mysql.h`、`FiberServer/db/mysql.cpp`。
- 已移除旧业务访问实现：`FiberServer/my/mysqlop.cpp`。
- 已移除 `USE_SOCI_DB` CMake 开关、旧 MySQL 条件编译分支和 `MySQLThreadIniter`。
- `scripts/docker_e2e.sh` 已扩展为服务级主链路验证脚本，覆盖重复注册、正确/错误登录、小文件直传、md5 秒传检查、两用户秒传引用、分片上传、文件列表、下载和删除。
- `SociManager::get()` 已支持连接复用和获取超时：按 `max_conn` 或兼容现有 `connection` 配置限制每个命名池的连接数，空闲连接通过 `SociDB::ptr` deleter 自动归还。
- SOCI 路径已给会同时修改 `file_info` 和 `file_shared` 的关键链路补事务：秒传、直传落库、分片合并后落库、删除文件记录和共享引用计数。

## 已验证

默认构建：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps fiberserver-dev \
  bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" && cmake --build build -j"$(nproc)"'
```

默认测试：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps fiberserver-dev \
  bash -lc './build/test'
```

旧 MySQL C API 删除后，默认构建和 `./build/test` 已通过。

SOCI 服务级 E2E：

```bash
docker compose -f docker-compose.dev.yml up -d --force-recreate fiberserver-app nginx
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://nginx \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e DOWNLOAD_HEADER_BASE_URL=http://fiberserver-app:8080 \
  -e CHUNK_UPLOAD_MODE=body \
  fiberserver-dev bash scripts/docker_e2e.sh
```

已通过，输出形态为：

```text
e2e passed: user=... second_user=... file_id=... instant_file_id=... chunk_file_id=... chunk_mode=body
```

旧 MySQL C API 删除后，服务级 E2E 已重新运行并通过。

SOCI 业务压测：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://nginx \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e REQUESTS=800 \
  -e CONCURRENCY=80 \
  -e UPLOAD_REQUESTS=10 \
  -e UPLOAD_CONCURRENCY=3 \
  fiberserver-dev bash scripts/docker_bench_business.sh
```

已在 SOCI 服务实例下通过，结果：

| 场景 | 请求数 | 并发 | 成功 | 错误 | QPS | P95 | P99 | 最大延迟 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| status | 800 | 80 | 800 | 0 | 806.50 | 82.65ms | 102.73ms | 122.37ms |
| login | 800 | 80 | 800 | 0 | 785.89 | 113.88ms | 136.89ms | 176.29ms |
| myfiles | 800 | 80 | 800 | 0 | 795.99 | 118.63ms | 156.49ms | 175.79ms |
| download | 800 | 80 | 800 | 0 | 901.73 | 91.49ms | 111.57ms | 173.23ms |
| direct_upload | 10 | 3 | 10 | 0 | 135.92 | 28.21ms | 28.21ms | 28.21ms |

补充小样本结果：

- 200 请求 / 20 并发：`status/login/myfiles/download/direct_upload` 全部 0 错误。
- 500 请求 / 50 并发：`status/login/myfiles/download/direct_upload` 全部 0 错误。
- 1000 请求 / 100 并发：`status/login/myfiles/download/direct_upload` 全部 0 错误；`login` P95 约 113.82ms，`download` P95 约 98.49ms，未复现 15 秒级超时。
- 压测后 `http://localhost:8081/api/status` 返回 `HTTP 200`，`global_queue_size=0`。

## 后续

### 1. 已完成验证

当前已经确认 SOCI 不只是能编译，而是在 MySQL + FastDFS + Nginx 的完整链路里行为正确。

脚本覆盖：

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

产出：

- 已补 `scripts/docker_e2e.sh`。
- 已运行 SOCI 服务实例，并通过 Nginx 入口完成真实链路验证。
- 已运行 20/50/80/100 并发业务压测，读接口和小文件直传均为 0 错误。

### 2. 后续增强

## 风险

- SOCI 是同步库，当前 100 并发业务压测未观察到错误；后续仍建议补更长时间压测。
- 当前 `SociManager` 连接池使用线程级等待，当前小/中样本未观察到连接池超时；真实生产并发下仍需要根据延迟和连接数调参。
- SOCI 数据库事务已覆盖 `file_info` 与 `file_shared` 的关键组合写入；删除路径中 FastDFS 文件删除发生在数据库事务提交之后，后续如要强化一致性，需要补失败补偿或清理任务。
- `filename + user` 定位文件的语义需要确认是否允许同名文件；如果允许同名，应改用 `file_id` 或记录 `id`。

## 当前推荐顺序

1. 视需要补更长时间 SOCI 压测和回归验证。
2. 视需要给 FastDFS 删除失败补补偿或异步清理任务。
