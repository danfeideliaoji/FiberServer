# FiberServer

FiberServer 是一个基于 C++17 的高并发 CI/CD 构建制品仓库。它面向流水线和部署系统提供构建产物上传、checksum 去重、分片上传、版本化元数据管理、高并发下载转发和项目 token 鉴权。

项目底层不是简单套一个文件 CRUD 服务，而是用 C++ 实现了一套借鉴 Go GMP 思想的 M:N 协程调度模型，并结合 `epoll`、系统调用 hook、HTTP servlet、FastDFS、SOCI/MySQL 和 Nginx 内部跳转，验证少量 OS 线程承载大量 IO 密集型请求的服务模型。

推荐表述：

> 基于 C++ 协程调度与异步 IO 的高并发构建制品仓库，面向 CI/CD 流水线提供构建产物上传、版本化元数据管理、checksum 去重、分片上传、项目 token 鉴权和高并发下载分发能力。底层借鉴 Go GMP 调度思想，实现 C++ M:N 协程调度器，并结合 epoll、hook、FastDFS、MySQL 和 Nginx 支撑 IO 密集型场景。

## 核心能力

- CI/CD 构建制品仓库语义：`project_name`、`version`、`build_no`、`artifact_name`、`branch`、`commit_id`
- 独立 `artifact_info` 元数据表，制品信息不再依赖旧文件名反推
- checksum 秒传和物理文件去重：`file_shared` 保存 checksum 到 FastDFS `file_id` 的映射
- 同一制品坐标不可变：同坐标同 checksum 复用，不同 checksum 返回冲突
- 项目 token 鉴权：artifact 写接口需要 `Authorization: Bearer <token>`
- 大文件分片上传，小文件直传，Nginx 可将大请求体落盘后转发给应用
- Nginx `X-Accel-Redirect` 下载转发，应用负责权限和元数据，文件数据面交给 Nginx/FastDFS
- Docker Compose 一键启动 MySQL、FastDFS tracker/storage、Nginx 和 FiberServer
- 单元测试、Docker e2e 和压测脚本覆盖主要链路

## 底层技术亮点

- 简化版 GMP 调度器：`Fiber` 对应 G，`Thread` 对应 M，`Processor` 对应 P
- 每个 `Processor` 维护本地运行队列，调度器保留全局队列
- 支持全局队列批量搬运、本地队列优先执行和 work stealing
- `IOManager + epoll` 负责 socket 事件和 timer 唤醒
- `hook.cpp` 将 `read/write/accept/connect/sleep` 等阻塞等待转成协程挂起
- 同步 SOCI/MySQL 调用隔离到 `DbExecutor`，避免 SQL 阻塞 HTTP worker
- `/api/status` 暴露调度器、队列、worker 和 Processor 运行统计

## 技术栈

- C++17
- Boost.Context
- epoll + syscall hook
- CMake
- yaml-cpp
- OpenSSL
- JsonCpp
- SOCI
- MySQL 8.0
- FastDFS
- Nginx
- Docker Compose

## 架构概览

```text
Client / CI Runner
        |
        v
Nginx ---------------> FastDFS storage
  |                         ^
  | proxy / internal        |
  v                         |
FiberServer HTTP servlet ---+
  |
  +-- GMP-style Scheduler / IOManager / hook
  |
  +-- DbExecutor + SOCI
        |
        v
      MySQL
      - user_info
      - file_info
      - file_shared
      - artifact_info
      - project_token
```

## 目录结构

```text
FiberServer/              核心源码
  base/                   日志、配置、锁、工具函数
  db/                     SOCI 数据库封装和数据库执行器
  my/                     业务层封装，包括 FastDFS、元数据、应用入口
  net/                    Socket、TCP、HTTP 协议和服务端实现
  servlets/               状态检查和制品仓库 HTTP 接口
  tests/                  单元测试和服务入口
docker/                   Docker、MySQL 初始化 SQL、Nginx 配置
docs/                     项目说明、答辩表述、运行说明、压测结果
scripts/                  Docker 构建、测试、运行、e2e 和压测脚本
wrk_bench/                wrk 压测脚本
```

## 快速开始

推荐使用 Docker Compose 运行，避免在本机手动安装 MySQL、FastDFS、SOCI 等依赖。

### 1. 构建开发镜像

```bash
bash scripts/docker_build.sh
```

### 2. 编译并运行单元测试

```bash
bash scripts/docker_test.sh
```

### 3. 启动服务

```bash
bash scripts/docker_run_server.sh
```

默认入口：

- FiberServer: `http://localhost:8080`
- Nginx 下载入口: `http://localhost:8081`
- MySQL: `localhost:3307`

### 4. 端到端验证

```bash
bash scripts/docker_e2e.sh
```

e2e 覆盖服务状态、旧公开接口 404、制品 token 创建、制品预检、直传/分片上传、latest/versions/builds 查询、下载、删除和坐标冲突保护。

## Artifact API

| 接口 | 说明 |
| --- | --- |
| `POST /api/artifacts/token` | 创建或轮换项目 token |
| `POST /api/artifacts/precheck` | 上传前检查，支持 checksum 秒传判断 |
| `POST /api/artifacts/upload/direct` | 小制品直传 |
| `POST /api/artifacts/upload/chunk` | 大制品分片上传 |
| `POST /api/artifacts/list` | 查询项目制品列表 |
| `POST /api/artifacts/checksum` | checksum 存在性检查 |
| `GET /api/artifacts/download` | 下载制品 |
| `POST /api/artifacts/delete` | 删除制品记录 |
| `GET /api/artifacts/latest` | 查询项目最新制品 |
| `GET /api/artifacts/versions` | 查询项目版本列表 |
| `GET /api/artifacts/builds` | 查询项目指定版本下的构建号列表 |

创建 token：

```bash
curl -X POST http://localhost:8080/api/artifacts/token \
  -H 'Content-Type: application/json' \
  -d '{"project_name":"auth-service","token":"ci-secret"}'
```

上传前检查：

```bash
curl -X POST http://localhost:8080/api/artifacts/precheck \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer ci-secret' \
  -d '{
    "project_name": "auth-service",
    "version": "1.2.0",
    "build_no": "104",
    "branch": "main",
    "commit_id": "8f31c9a",
    "artifact_name": "auth-service.tar.gz",
    "checksum": "abc123",
    "size": 4096
  }'
```

## 保留接口

| 接口 | 说明 |
| --- | --- |
| `GET /api/status` | 服务和调度器状态 |

旧的普通文件公开接口已移除，包括 `/api/register`、`/api/login`、`/api/upload`、`/api/upload/dirupload`、`/api/uploadchunk`、`/api/myfiles`、`/api/md5`、`/api/download` 和 `/api/deletefile`。当前业务层只对外保留状态接口和 `/api/artifacts/*` 制品仓库接口。

## 压测

基础压测：

```bash
bash scripts/docker_bench.sh
```

已有压测记录见 [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md)。

## 相关文档

- [docs/PROJECT_PRESENTATION.md](docs/PROJECT_PRESENTATION.md): 简历和答辩表述
- [docs/ARTIFACT_REPOSITORY.md](docs/ARTIFACT_REPOSITORY.md): 构建制品仓库接口和数据模型
- [docs/GMP_IMPLEMENTATION_SUMMARY.md](docs/GMP_IMPLEMENTATION_SUMMARY.md): GMP 调度器实现总结
- [docs/WSL2_DOCKER.md](docs/WSL2_DOCKER.md): WSL2 和 Docker 开发环境说明
- [docs/FASTDFS_HOOK.md](docs/FASTDFS_HOOK.md): FastDFS hook 并发问题验证
- [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md): 压测结果记录

## 表述边界

不要把项目描述成“完整复刻 Go runtime”或“完整实现 Artifactory/Nexus”。更准确的说法是：本项目实现了借鉴 Go GMP 思想的简化 C++ M:N 协程调度器，并在轻量级 CI/CD 构建制品仓库场景中验证高并发 IO 链路。

## License

当前仓库尚未声明许可证。如需开源发布，请在发布前补充 `LICENSE` 文件。
