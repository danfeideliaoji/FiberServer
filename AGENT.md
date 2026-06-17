# AGENT.md

本文件用于约束后续 coding agent 在本项目中的工作方式，避免在核心调度器、网络 IO 和云存储业务之间做过度或不一致的改动。

## 项目定位

本项目是一个基于 C++ 用户态协程、epoll 异步 IO、HTTP 服务、FastDFS 和 MySQL 的高并发云存储系统。

后续 GMP 方向的准确表述是：

> 基于 C++ 协程调度器的高并发云存储系统：借鉴 Go GMP 的异步 IO 调度模型

不要把项目描述为“完整复刻 Go runtime”或“完整实现 Go GMP 调度器”。当前目标是实现适合本项目的简化版 GMP 调度模型，用于支撑云存储业务和高并发 IO 场景。

## 重点目录和文件

核心调度与协程：

- `FiberServer/fiber.h`
- `FiberServer/fiber.cpp`
- `FiberServer/scheduler.h`
- `FiberServer/scheduler.cpp`
- `FiberServer/iomanager.h`
- `FiberServer/iomanager.cpp`
- `FiberServer/hook.h`
- `FiberServer/hook.cpp`
- `FiberServer/thread.h`
- `FiberServer/thread.cpp`

网络与 HTTP：

- `FiberServer/net/tcp_server.h`
- `FiberServer/net/tcp_server.cpp`
- `FiberServer/net/http/http_server.h`
- `FiberServer/net/http/http_server.cpp`
- `FiberServer/net/http/servlet.h`
- `FiberServer/net/http/servlet.cpp`

云存储业务：

- `FiberServer/servlets/`
- `FiberServer/my/fastdfs.h`
- `FiberServer/my/fastdfs.cpp`
- `FiberServer/my/mysqlop.h`
- `FiberServer/my/mysqlop.cpp`
- `FiberServer/my/chunkManager.h`
- `FiberServer/my/chunkManager.cpp`
- `FiberServer/my/application.h`
- `FiberServer/my/application.cpp`

计划文档：

- `docs/WSL2_DOCKER.md`

## GMP 改造边界

建议实现的简化版 GMP 能力：

- `Fiber` 对应 Go GMP 中的 `G`。
- `Thread` 对应 Go GMP 中的 `M`。
- 新增 `Processor` 对应 Go GMP 中的 `P`。
- 每个 `Processor` 维护一个本地运行队列。
- `Scheduler` 保留全局运行队列作为兜底。
- 调度顺序建议为：本地队列 -> 全局队列 -> work stealing -> idle。
- `IOManager + epoll` 作为简化版 netpoll。
- `hook.cpp` 继续负责把阻塞 IO 转成协程挂起和事件唤醒。

暂不建议实现：

- Go runtime 级别的抢占式调度。
- GC 协作和 safepoint。
- 系统调用阻塞时完整解绑 M/P。
- 完整复刻 Go netpoller。
- 跨平台 IOCP/kqueue。
- 大规模重写 HTTP server、FastDFS 和 MySQL 业务层。

## 修改原则

核心调度器改动必须小步推进：

1. 先补测试或最小验证代码。
2. 再改 `Scheduler` 或 `IOManager`。
3. 每个阶段保持可编译、可运行。
4. 不要一次性重构调度、HTTP、存储和数据库多个层次。
5. 不要为了套 GMP 名词破坏已有上传、下载、秒传、分片上传功能。

涉及以下文件时要特别谨慎：

- `FiberServer/scheduler.h`
- `FiberServer/scheduler.cpp`
- `FiberServer/iomanager.h`
- `FiberServer/iomanager.cpp`
- `FiberServer/hook.cpp`

这些文件之间有强耦合，改动后必须验证：

- 普通协程任务可以调度。
- `sleep/usleep/nanosleep` hook 正常。
- socket `accept/read/write/connect` hook 正常。
- `IOManager::idle()` 不会卡死。
- `Scheduler::stop()` 不会提前停止或永远无法停止。

## 调度器实现注意事项

当前 `Scheduler` 使用全局队列 `m_fibers`。引入 `Processor` 后，停止条件和唤醒逻辑必须同步修改。

新增本地队列后，`stopping()` 不能只检查全局队列，还要检查所有 `Processor` 的本地队列是否为空。

指定线程任务 `FiberAndThread::thread != -1` 的语义不能被破坏。第一版 GMP 改造中，指定线程任务可以继续走全局队列，并在取任务时检查线程 id。

`tickle()` 不能只依赖全局队列是否为空。任务进入本地队列、全局队列或被 steal 后，都要考虑是否需要唤醒 idle 线程。

`IOManager::idle()` 负责 `epoll_wait`，不要让线程在本地队列还有任务时进入长期阻塞。

## 构建与验证

项目使用 CMake 构建，入口文件包括：

- `CMakeLists.txt`
- `FiberServer/tests/server_main.cpp`
- `FiberServer/tests/test.cpp`

推荐开发环境是 WSL2 Ubuntu + Docker Compose。容器化开发文件包括：

- `docker/Dockerfile.dev`
- `docker-compose.dev.yml`
- `docker/config.docker.yml`
- `docker/fdfs/client.conf`
- `docker/mysql-init/001_schema.sql`
- `scripts/docker_build.sh`
- `scripts/docker_test.sh`
- `scripts/docker_e2e.sh`
- `scripts/docker_run_server.sh`

`docker/Dockerfile.dev` 从源码构建 FastDFS client 依赖，当前需要同时安装 `libfastcommon`、`libserverframe` 和 `fastdfs`。

后续修改后建议至少做以下验证：

- 编译通过。
- 调度器基本测试通过。
- HTTP server 能启动。
- 上传、下载、文件列表接口可用。
- wrk 压测无明显错误率。

如果当前环境缺少依赖导致无法完整编译，需要在最终说明中明确写出未验证原因。

在 WSL2 + Docker 环境中，优先使用：

```bash
bash scripts/docker_test.sh
```

服务主链路验证使用：

```bash
bash scripts/docker_e2e.sh
```

运行服务使用：

```bash
bash scripts/docker_run_server.sh
```

## 推荐项目表述

简历、答辩或 README 中推荐使用：

> 基于 C++ 协程调度与异步 IO 的高并发云存储系统

如果需要强调 GMP：

> 借鉴 Go GMP 调度思想，实现了 C++ M:N 协程调度器，并结合 epoll 和 hook 机制支撑高并发云存储服务。

避免使用：

> 完整复刻 Go GMP

除非项目后续真的实现了抢占式调度、M/P 解绑、完整 netpoller 和对应测试。
