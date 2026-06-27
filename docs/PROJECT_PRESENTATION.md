# FiberServer 项目表述与答辩要点

这份文档用于简历、开题/答辩和面试介绍。重点是把项目从“普通云存储”重新定位为“CI/CD 构建制品仓库”，同时把底层 C++ GMP 风格协程调度作为技术亮点讲清楚。

## 一句话介绍

FiberServer 是一个基于 C++17 协程调度与异步 IO 的高并发 CI/CD 构建制品仓库，支持构建产物上传、版本化元数据管理、checksum 去重、分片上传、项目 token 鉴权和 Nginx/FastDFS 下载分发。

## 简历表述

可以写成：

> 基于 C++17 实现高并发 CI/CD 构建制品仓库，底层借鉴 Go GMP 思想实现 M:N 协程调度器，结合 epoll、系统调用 hook、FastDFS、SOCI/MySQL 和 Nginx，支持构建产物上传、checksum 秒传去重、分片上传、版本/构建号元数据管理、项目 token 鉴权和高并发下载转发；通过 Docker Compose 构建 MySQL、FastDFS、Nginx 和应用的一体化验证环境，并补充单元测试、端到端测试和压测脚本。

更短的版本：

> C++17 高并发构建制品仓库：实现简化版 GMP 风格 M:N 协程调度器，基于 epoll/hook 支撑同步写法下的异步 IO，并结合 FastDFS、MySQL 和 Nginx 完成 CI/CD 制品上传、去重、分片、版本化查询和下载分发。

## 不建议的表述

- 不要说“云存储系统”作为主定位。这个说法太常见，容易和网盘/对象存储 CRUD 混在一起。
- 不要说“完整复刻 Go runtime”。准确说法是“借鉴 Go GMP 思想的简化版 M:N 协程调度模型”。
- 不要说“完整实现 Artifactory/Nexus”。当前是轻量级制品仓库，不包含 Maven/NPM/Docker Registry 等完整包管理协议。
- 不要说“数据库和 FastDFS 强一致”。当前数据库事务能保证元数据内部一致，外部存储失败仍需要补偿任务。

## 项目为什么不是普通文件服务

普通文件服务通常围绕用户、文件名、上传、下载和删除展开。FiberServer 当前的主业务语义是构建制品仓库：

- 制品有项目名、版本号、构建号、制品名、分支和 commit id。
- 同一个项目可以保留多个版本和多个 build。
- 制品坐标 `project_name + version + build_no + artifact_name` 不允许被不同 checksum 静默覆盖。
- 上传前可以通过 checksum 秒传判断，避免重复上传相同构建产物。
- 写接口需要项目 token，更接近 CI runner 使用场景。
- 下载数据面交给 Nginx/FastDFS，应用只负责鉴权和元数据判断。

## 答辩时的讲法

可以按这个顺序讲：

1. **业务定位**：面向 CI/CD 流水线保存构建产物，而不是普通网盘。
2. **核心接口**：token 创建、precheck、direct/chunk upload、list、download、delete、latest、versions、builds。
3. **元数据模型**：`artifact_info` 管制品坐标，`file_shared` 管物理文件去重，`project_token` 管项目上传 token。
4. **高并发基础**：C++ fiber、简化 GMP 调度、epoll、hook，把阻塞 IO 等待转换成协程挂起。
5. **外部资源隔离**：SOCI/MySQL 是同步库，所以通过 `DbExecutor` 隔离，避免 SQL 阻塞 HTTP worker。
6. **下载链路**：应用返回 `X-Accel-Redirect`，Nginx 内部转发到 FastDFS，避免应用进程搬运大文件。
7. **验证方式**：Docker Compose 一键启动依赖，单元测试保护调度器，e2e 验证完整业务链路，压测记录性能边界。

## 技术亮点拆解

### 1. GMP 风格调度

项目实现了 `Fiber + Thread + Processor` 的 M:N 调度模型。`Fiber` 是用户态协程，`Thread` 是 OS 工作线程，`Processor` 持有本地运行队列。调度器优先执行本地队列，必要时从全局队列批量搬运任务，也支持 work stealing，降低全局队列竞争。

### 2. epoll 与 hook

`IOManager` 基于 epoll 管理 socket 事件和 timer。`hook.cpp` 拦截 `read/write/accept/connect/sleep` 等阻塞调用，将等待转换为事件注册和 fiber 挂起。这样业务代码可以保持同步写法，但底层等待不会长期占住 OS worker。

### 3. 制品仓库模型

`artifact_info` 保存制品业务元数据，`file_shared` 保存 checksum 到物理 `file_id` 的映射和引用计数。相同 checksum 可以复用物理文件；同一制品坐标如果 checksum 不同会返回冲突，避免错误覆盖。

### 4. Docker 可复现环境

项目通过 Docker Compose 统一启动 MySQL、FastDFS tracker、FastDFS storage、Nginx 和 FiberServer。e2e 脚本覆盖服务状态、旧公开接口移除、制品 token、制品上传、查询、下载、删除和冲突保护。

## 可以展示的 API 流程

```bash
# 1. 创建项目 token
curl -X POST http://localhost:8080/api/artifacts/token \
  -H 'Content-Type: application/json' \
  -d '{"project_name":"auth-service","token":"ci-secret"}'

# 2. 上传前检查
curl -X POST http://localhost:8080/api/artifacts/precheck \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer ci-secret' \
  -d '{"project_name":"auth-service","version":"1.2.0","build_no":"104","artifact_name":"auth-service.tar.gz","checksum":"abc123","size":4096}'

# 3. 查询最新制品
curl 'http://localhost:8080/api/artifacts/latest?project_name=auth-service'

# 4. 查询版本列表
curl 'http://localhost:8080/api/artifacts/versions?project_name=auth-service'
```

完整接口说明见 [ARTIFACT_REPOSITORY.md](ARTIFACT_REPOSITORY.md)。

## 可能被问到的问题

### 为什么不用现成对象存储或 Nexus？

项目目标不是替代成熟商业系统，而是用一个具体业务场景验证 C++ 协程调度、异步 IO、数据库隔离和大文件分发链路。选择制品仓库，是因为它比普通云存储更强调版本、build、checksum、不可变坐标和 CI 自动化，更能体现后端系统设计。

### 为什么数据库访问还要单独 DbExecutor？

SOCI/MySQL 是同步访问库，SQL 执行过程不一定能完全通过 hook 变成非阻塞。如果直接在 HTTP worker 中执行 SQL，慢查询会占住服务线程。`DbExecutor` 把同步数据库调用隔离到独立 DB 调度器，HTTP worker 只等待结果恢复。

### 为什么下载交给 Nginx？

大文件下载会长时间占用连接和带宽。应用层只需要完成权限和元数据判断，随后返回 `X-Accel-Redirect`。Nginx 内部转发到 FastDFS storage，避免 C++ 应用进程自己搬运文件数据。

### 目前还有哪些边界？

当前项目是轻量级制品仓库，不包含完整包管理协议、企业级 RBAC、审计日志、异步清理补偿和跨节点一致性治理。后续如果生产化，优先补上传后 checksum 校验、FastDFS 孤儿文件清理、删除 outbox、token 管理后台和可观测性指标。
