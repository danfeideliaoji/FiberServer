# WSL2 + Docker 开发环境

本项目依赖 Linux 协程和网络能力，包括 Boost.Context、`epoll`、`pthread`。推荐在 WSL2 Ubuntu 中开发，并用 Docker Compose 管理 MySQL、FastDFS 和 Nginx。

相关文档：

- [文档索引](README.md)
- [GMP 改造说明](GMP_IMPLEMENTATION_SUMMARY.md)
- [压测结果记录](BENCHMARK_RESULTS.md)

## 推荐目录

建议把项目放在 WSL2 的 Linux 文件系统中，而不是长期在 `/mnt/c` 或 `/mnt/e` 下编译：

```bash
mkdir -p ~/projects
cp -a /mnt/e/project/FiberServer-main/FiberServer-main ~/projects/FiberServer-main
cd ~/projects/FiberServer-main
```

## 启动依赖服务

```bash
docker compose -f docker-compose.dev.yml up -d mysql fastdfs-tracker fastdfs-storage
```

## 构建开发镜像

```bash
docker compose -f docker-compose.dev.yml build fiberserver-dev
```

等价脚本：

```bash
bash scripts/docker_build.sh
```

## 编译并运行测试

```bash
docker compose -f docker-compose.dev.yml run --rm fiberserver-dev bash -lc \
  'cmake -S . -B build -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" && cmake --build build -j"$(nproc)" && ./build/test'
```

等价脚本：

```bash
bash scripts/docker_test.sh
```

默认构建使用 Release + SOCI 数据库后端，旧 MySQL C API 路径已移除。需要 Debug 时可设置 `CMAKE_BUILD_TYPE=Debug`。

## 运行服务

```bash
bash scripts/docker_run_server.sh
```

服务启动后运行主链路验证：

```bash
bash scripts/docker_e2e.sh
```

`docker-compose.dev.yml` 默认以 SOCI 数据库后端构建并启动 `fiberserver-app`。

分片上传默认使用 request body 直传模式：

```bash
CHUNK_UPLOAD_MODE=body bash scripts/docker_e2e.sh
```

如果要验证 `X-File-Path` 落盘模式：

```bash
CHUNK_UPLOAD_MODE=file bash scripts/docker_e2e.sh
```

运行轻量压测：

```bash
BASE_URL=http://localhost:8080 REQUESTS=200 CONCURRENCY=20 bash scripts/docker_bench.sh
```

也可以在 Compose 网络中运行：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e CHUNK_UPLOAD_MODE=body \
  fiberserver-dev bash scripts/docker_e2e.sh
```

通过 Nginx 验证落盘分片上传时，把上传入口也切到 `http://nginx`：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://nginx \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e DOWNLOAD_HEADER_BASE_URL=http://fiberserver-app:8080 \
  -e CHUNK_UPLOAD_MODE=body \
  fiberserver-dev bash scripts/docker_e2e.sh
```

Compose 网络内压测：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e REQUESTS=200 \
  -e CONCURRENCY=20 \
  fiberserver-dev bash scripts/docker_bench.sh
```

业务接口压测：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e REQUESTS=30 \
  -e CONCURRENCY=5 \
  -e UPLOAD_REQUESTS=5 \
  -e UPLOAD_CONCURRENCY=2 \
  fiberserver-dev bash scripts/docker_bench_business.sh
```

压测脚本默认使用 HTTP keep-alive，每个 worker 复用自己的连接。需要回归短连接基线时可显式关闭：

```bash
BENCH_KEEPALIVE=0 docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://nginx \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e REQUESTS=300 \
  -e CONCURRENCY=30 \
  fiberserver-dev bash scripts/docker_bench_business.sh
```

业务接口矩阵压测：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e REQUESTS_LIST="30 100" \
  -e CONCURRENCY_LIST="1 5 10" \
  -e UPLOAD_REQUESTS=5 \
  -e UPLOAD_CONCURRENCY=2 \
  fiberserver-dev bash scripts/docker_bench_matrix.sh
```

服务监听：

- FiberServer: `http://localhost:8080`
- Nginx proxy: `http://localhost:8081`，需要另开终端执行 `docker compose -f docker-compose.dev.yml up nginx`
- MySQL: `localhost:3307`，容器内部仍使用 `mysql:3306`
- FastDFS tracker: `localhost:22122`

## 配置文件

容器环境使用：

```text
docker/config.docker.yml
```

应用通过环境变量读取配置：

```bash
FIBER_CONFIG=/workspace/docker/config.docker.yml
```

服务线程数默认由配置项控制：

```yaml
server:
  worker_threads: 5
```

Docker Compose 支持用环境变量临时覆盖：

```bash
FIBER_WORKER_THREADS=2 docker compose -f docker-compose.dev.yml up -d --build --force-recreate fiberserver-app
```

性能分段日志默认开启，会输出到 `perf` logger。日志形态包括：

```text
perf route=/api/login status=ok total_ms=1 db_ms=1 fastdfs_ms=0 file_io_ms=0
perf component=soci_pool name=file_info source=idle pool_wait_ms=0 create_ms=0 total_ms=0 pool_total=1 pool_idle=0
```

查看最近性能日志：

```bash
docker compose -f docker-compose.dev.yml logs --tail=200 fiberserver-app | grep 'perf '
```

如果要跑纯吞吐压测，建议临时关闭性能日志，避免 stdout IO 影响 QPS：

```bash
FIBER_PERF_LOG=0 docker compose -f docker-compose.dev.yml up -d --build --force-recreate fiberserver-app
```

如果没有设置 `FIBER_CONFIG`，程序回退读取：

```text
docker/config.docker.yml
```

## 依赖说明

`docker/Dockerfile.dev` 安装 C++ 构建依赖，并从源码构建：

- `libfastcommon`
- `libserverframe`
- `fastdfs`

Compose 服务使用：

- `mysql:8.0`
- `delron/fastdfs`
- `nginx:1.25`

FastDFS client 配置在：

```text
docker/fdfs/client.conf
```

FastDFS 调用跑在 syscall hook 模式下时，需要保持连接池开启：

```conf
use_connection_pool=true
```

不要在 hook 模式下改回 `false`。关闭连接池时，FastDFS C 客户端可能把同一个全局 tracker 连接槽返回给多个并发协程，导致多个协程同时等待同一个 fd 的读写事件。详细验证记录见 [FastDFS Hook Verification](FASTDFS_HOOK.md)。

## 已验证状态

更新时间：2026-06-11

当前机器已完成以下验证：

- Docker Desktop 已安装并启动，Docker CLI 版本为 `29.5.3`，Compose 版本为 `v5.1.4`。
- `docker compose -f docker-compose.dev.yml config` 校验通过。
- `fiberserver-dev:local` 镜像构建成功。
- 容器内 CMake 编译和 `./build/test` 通过，调度器测试输出 `gmp scheduler test passed: 200 tasks`。
- `mysql`、`fastdfs-tracker`、`fastdfs-storage` 已能启动，MySQL healthcheck 为 healthy。
- `fiberserver-app` 已能启动，`http://localhost:8080/api/_/config` 返回 `HTTP/1.1 200 OK`。
- `http://localhost:8080/api/status` 返回 `HTTP/1.1 200 OK`，可查看当前调度器统计，包括每个 P 的本地队列、执行来源、全局队列批量搬运、steal 任务数、steal 批次数、steal 尝试和失败次数。
- Nginx 已配置 `/group1/` 内部转发到 FastDFS storage，支持 `X-Accel-Redirect` 完整下载。
- Nginx 已配置 `/api/uploadchunk` 的 `client_body_in_file_only` 落盘转发，并和 `fiberserver-app` 共享 `/var/data/tmp_uploads`。
- 注册、重复注册、正确/错误登录、直传上传、md5 秒传检查、两用户秒传引用、分片上传、文件列表、下载响应头、删除和 Nginx 完整下载主链路已通过 `scripts/docker_e2e.sh` 覆盖。
- `scripts/docker_bench.sh` 已可在 Compose 网络内运行，当前小样本基线为 `/api/status` 100 请求、10 并发、全部 200、QPS 约 765、P95 约 19ms。
- `scripts/docker_bench_business.sh` 已可在 Compose 网络内运行，当前小样本基线为 30 请求、5 并发：`status` QPS 约 789、`login` QPS 约 753、`myfiles` QPS 约 758、`download` QPS 约 539；直传上传 5 请求、2 并发 QPS 约 35，全部 0 错误。
- `scripts/docker_bench_matrix.sh` 已可在 Compose 网络内运行，已用 `REQUESTS_LIST="10 20"`、`CONCURRENCY_LIST="1 5"` 验证多轮连续业务压测全部 0 错误。
- `FIBER_WORKER_THREADS=2` 已验证可覆盖服务线程数，`/api/status` 显示 2 个 Processor；随后已恢复默认 5 线程运行状态。

## 注意事项

- 首次构建需要联网拉取 apt 包、GitHub 源码和 Docker 镜像。
- 如果 FastDFS 镜像或源码上游不可用，需要固定到可用 tag 或改用内部镜像。
- 本环境用于开发和验证，不是生产部署配置。
- GMP 调度器修改后，优先运行 `scripts/docker_test.sh` 验证调度器测试。
- `scripts/docker_test.sh` 使用 `--no-deps`，只编译项目和运行本地测试，不启动 MySQL/FastDFS 服务。
