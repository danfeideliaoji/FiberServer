# FiberServer

FiberServer 是一个基于 C++17 的文件服务项目，核心包括协程/调度器、HTTP 服务、FastDFS 文件存储、SOCI/MySQL 数据库访问，以及 Docker 化的开发和验证环境。

项目当前主要覆盖用户注册、登录、文件上传、秒传、分片上传、文件列表、下载和删除等接口，适合作为 C++ 网络服务、协程调度、文件存储链路和数据库访问封装的学习与实验项目。

## 功能特性

- C++17 实现的 fiber/coroutine 调度器
- 基于 TCP/HTTP 的服务端框架
- Servlet 风格的接口路由
- FastDFS 文件存储接入
- SOCI + MySQL 数据库访问层
- 文件 MD5 秒传检查
- 普通上传和分片上传
- Nginx 下载转发支持
- Docker Compose 一键启动开发依赖
- 端到端测试和压测脚本

## 技术栈

- C++17
- CMake
- Boost.Context
- yaml-cpp
- OpenSSL
- JsonCpp
- SOCI
- MySQL 8.0
- FastDFS
- Nginx
- Docker Compose

## 目录结构

```text
FiberServer/              核心源码
  base/                   日志、配置、锁、工具基础设施
  db/                     SOCI 数据库封装和数据库执行器
  my/                     业务层封装，包括 FastDFS、文件信息、应用入口
  net/                    Socket、TCP、HTTP 协议和服务端实现
  servlets/               注册、登录、上传、下载、删除等接口处理
  tests/                  测试入口和服务入口
docker/                   Docker 配置、MySQL 初始化 SQL、Nginx 配置
docs/                     项目说明、迁移记录、压测结果
plans/                    开发计划和历史记录
scripts/                  Docker 构建、测试、运行、端到端验证和压测脚本
wrk_bench/                wrk 压测脚本
```

## 快速开始

推荐使用 Docker Compose 运行，避免在本机手动安装 MySQL、FastDFS、SOCI 等依赖。

### 1. 构建开发镜像

```bash
bash scripts/docker_build.sh
```

### 2. 编译并运行测试

```bash
bash scripts/docker_test.sh
```

### 3. 启动服务

```bash
bash scripts/docker_run_server.sh
```

服务默认监听：

- FiberServer: `http://localhost:8080`
- Nginx 下载入口: `http://localhost:8081`
- MySQL: `localhost:3307`

### 4. 端到端验证

服务启动后，可以运行：

```bash
bash scripts/docker_e2e.sh
```

端到端脚本会覆盖注册、登录、上传、秒传、文件列表、下载、删除和分片上传流程。

## 常用接口

| 接口 | 说明 |
| --- | --- |
| `GET /api/status` | 服务和调度器状态 |
| `POST /api/register` | 用户注册 |
| `POST /api/login` | 用户登录 |
| `POST /api/upload` | 上传预检查和秒传判断 |
| `POST /api/upload/dirupload` | 普通文件上传 |
| `POST /api/uploadchunk` | 分片上传 |
| `POST /api/md5` | MD5 文件检查 |
| `POST /api/myfiles` | 用户文件列表 |
| `GET /api/download` | 文件下载 |
| `POST /api/deletefile` | 删除文件 |

## 配置

Docker 环境的配置文件是：

```text
docker/config.docker.yml
```

常用配置包括：

- `server.worker_threads`: 服务工作线程数
- `db.worker_threads`: 数据库工作线程数
- `mysql.dbs`: MySQL 连接配置
- `uploadfiles.chunk_sizes`: 分片大小
- `uploadfiles.tmp_path`: 分片临时目录
- `fastdfs.conf_path`: FastDFS 客户端配置路径

运行时可以通过环境变量覆盖部分参数，例如：

```bash
FIBER_WORKER_THREADS=5 FIBER_DB_WORKER_THREADS=20 bash scripts/docker_run_server.sh
```

## 压测

基础压测：

```bash
bash scripts/docker_bench.sh
```

业务接口压测：

```bash
bash scripts/docker_bench_business.sh
```

不同并发档位压测：

```bash
bash scripts/docker_bench_rate.sh
```

已有压测记录见：

```text
docs/BENCHMARK_RESULTS.md
```

## 相关文档

- `docs/README.md`: 文档索引
- `docs/WSL2_DOCKER.md`: WSL2 和 Docker 开发环境说明
- `docs/SOCI_MIGRATION.md`: SOCI 数据库迁移记录
- `docs/FASTDFS_HOOK.md`: FastDFS hook 相关说明
- `docs/GMP_IMPLEMENTATION_SUMMARY.md`: GMP 调度器实现总结
- `docs/BENCHMARK_RESULTS.md`: 压测结果记录

## 当前状态

项目已迁移到 SOCI 数据库访问层，并移除了旧的 MySQL C API 业务路径。Docker 开发环境、端到端验证脚本和压测脚本已经补齐，当前仍适合作为开发和实验项目继续完善。

## License

当前仓库尚未声明许可证。如需开源发布，请在发布前补充 `LICENSE` 文件。
