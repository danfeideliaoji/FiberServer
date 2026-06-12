# FiberServer GMP 改造说明

本文档记录从接手项目到实现简化版 GMP 调度器期间完成的主要工作，重点说明“原项目是什么状态”“为什么要改”“具体改了哪些模块”“目前验证到什么程度”。

相关文档：

- [文档索引](README.md)
- [WSL2 + Docker 开发环境](WSL2_DOCKER.md)
- [压测结果记录](BENCHMARK_RESULTS.md)

## 1. 原项目基础

原项目已经具备一个较完整的 C++ 云存储服务雏形，核心能力包括：

- 用户态协程 `Fiber`，基于 `ucontext` 保存和切换执行上下文。
- 多线程协程调度器 `Scheduler`，负责在线程池中执行协程任务。
- `IOManager`，基于 `epoll` 管理 socket 读写事件和定时器。
- `hook` 层，拦截 `read/write/accept/connect/sleep` 等阻塞调用，把阻塞等待转成协程挂起和事件唤醒。
- HTTP server、servlet 路由、登录注册、文件列表、上传、下载等业务接口。
- MySQL 存储用户和文件元数据，FastDFS 存储文件内容。

从调度模型上看，原项目已经是：

```text
C++ 用户态协程 + 多线程调度 + epoll 异步 IO + 阻塞 IO hook
```

但它还不是 GMP 风格调度器。原来的调度器主要依赖一个全局任务队列，多线程共同抢这个队列里的任务，缺少显式的 `P` 抽象、本地运行队列、全局队列和本地队列协同、work stealing 等机制。

## 2. 改造目标

这次目标不是完整复刻 Go runtime，而是在现有 C++ 协程框架上实现一个能运行、能解释、能测试、能压测的简化版 GMP 模型。

对应关系如下：

| Go GMP 概念 | 本项目实现 | 说明 |
| --- | --- | --- |
| G | `Fiber` | 用户态协程，一个可被调度的任务 |
| M | `Thread` | OS 工作线程，实际执行协程 |
| P | `Processor` | 新增调度处理器，持有本地运行队列 |
| global run queue | `Scheduler::m_fibers` | 外部提交任务和兜底队列 |
| local run queue | `Processor::run_queue` | 每个 P 的本地任务队列 |
| netpoll | `IOManager + epoll` | IO 就绪后重新调度协程 |
| blocking syscall handling | `hook.cpp` | 把阻塞 IO 转成协程挂起和事件唤醒 |

项目表述更准确地说是：

```text
基于 C++ 协程调度器的高并发云存储系统：借鉴 Go GMP 思想实现 M:N 协程调度、epoll 事件驱动和 work stealing。
```

## 3. 开发环境与运行链路

为了让项目能稳定构建、运行和压测，先补齐了 Docker 开发环境：

- 新增/完善 `docker-compose.dev.yml`，统一启动 MySQL、FastDFS tracker、FastDFS storage、Nginx 和 FiberServer。
- 新增 `docker/Dockerfile.dev`，在容器内安装构建依赖，并从源码构建 FastDFS 相关依赖。
- 新增 `docker/config.docker.yml`，让容器内服务使用 Docker 网络里的 MySQL/FastDFS 地址。
- 新增 `docs/WSL2_DOCKER.md`，记录 WSL2 + Docker Compose 的构建、测试、运行、压测流程。
- 新增脚本：
  - `scripts/docker_build.sh`
  - `scripts/docker_test.sh`
  - `scripts/docker_run_server.sh`
  - `scripts/docker_e2e.sh`
  - `scripts/docker_bench.sh`
  - `scripts/docker_bench_business.sh`
  - `scripts/docker_bench_matrix.sh`

这一步的价值是把项目从“只能读代码”推进到“能在容器里稳定编译、运行、测试、压测”。

## 4. 简化版 GMP 调度器实现

核心修改集中在：

- `FiberServer/scheduler.h`
- `FiberServer/scheduler.cpp`

### 4.1 新增 Processor

新增 `Processor` 抽象，作为简化版 GMP 里的 `P`。

每个 `Processor` 维护：

- `id`
- 本地任务队列 `run_queue`
- 本地投递次数
- 执行次数
- 本地执行、全局执行、steal 执行统计
- 全局批量拉取统计
- steal 成功、批次、尝试、失败统计

`Scheduler` 初始化时根据参与调度的线程数创建一组 `Processor`。工作线程进入 `Scheduler::run()` 后绑定自己的 `Processor`。

### 4.2 调整任务投递策略

原来任务主要进入全局队列。改造后策略变成：

1. 当前线程已经绑定本调度器的 `Processor`，并且任务没有指定线程时，优先放入当前 P 的本地队列。
2. 外部线程提交的任务继续放入全局队列。
3. 指定线程任务继续放入全局队列，保留原有指定线程语义。

这样可以减少全局队列锁竞争，让协程在同一个工作线程附近继续执行，接近 Go GMP 的本地队列思想。

### 4.3 调整取任务顺序

调度循环的取任务顺序改为：

```text
当前 P 本地队列 -> Scheduler 全局队列 -> 从其他 P work stealing -> idle
```

这样每个工作线程优先消费自己的本地队列；本地没有任务时，先尝试全局队列；全局也没有任务时，再去其他 P 的本地队列偷任务。

### 4.4 全局队列批量搬运

全局队列不再每次只取一个任务。现在当前 P 从全局队列拿到一个可执行任务后，会顺带搬运一批普通任务到当前 P 的本地队列。

这个改动的目的：

- 减少频繁抢全局锁。
- 让外部提交的大量任务能快速下沉到本地队列。
- 让后续执行更多走 local path。

指定线程任务仍保留在全局队列语义里，避免破坏原有行为。

### 4.5 work stealing

新增简化版 work stealing：

1. 当前 P 本地队列为空。
2. 全局队列也拿不到可执行任务。
3. 从其他 P 的本地队列尝试偷取任务。
4. 从目标 P 队尾偷取约一半任务。
5. 当前线程立即执行其中一个，其余放入当前 P 本地队列。

后续又做了两个增强：

- victim 起点轮转，避免每次都从同一个 P 开始扫描。
- 增加 steal attempt/fail 统计，方便观察空转和失败比例。

### 4.6 停止条件修正

新增本地队列后，`stopping()` 不能只看全局队列。现在停止条件会同时检查：

- 全局队列是否为空。
- 所有 P 的本地队列是否为空。
- 是否还有活跃线程。

这样避免本地队列还有任务时调度器提前退出。

## 5. 状态观测能力

为了让 GMP 改造能被看见，新增了调度器状态快照：

- `Scheduler::getStats()`
- `Scheduler::dump()`

并新增 `/api/status` HTTP 接口，涉及文件：

- `FiberServer/servlets/status_servlet.h`
- `FiberServer/servlets/status_servlet.cpp`
- `FiberServer/servlets/all_servlet.h`

接口返回内容包括：

- 调度器是否可用。
- 全局队列长度。
- 全局投递次数。
- 活跃线程数。
- 空闲线程数。
- 每个 P 的本地队列长度。
- 每个 P 的本地投递次数。
- 每个 P 的执行次数。
- local/global/steal 执行来源拆分。
- global pull/global batch 次数。
- steal 次数、steal batch、steal attempt、steal fail。

这让后续压测时可以判断任务到底是从本地队列执行、全局队列执行，还是通过偷取执行。

## 6. 测试补充

主要测试在：

- `FiberServer/tests/test.cpp`

新增或增强了这些测试：

- `test_gmp_scheduler()`：验证大量协程任务可以通过 GMP 调度模型正常执行，并检查执行统计。
- `test_gmp_batch_stealing()`：构造单个 P 本地队列堆积场景，验证其他 P 可以批量偷取任务。
- `test_iomanager_sleep_timer()`：验证 `usleep` hook 能通过 `IOManager` timer 唤醒协程。
- `test_iomanager_socket_hook()`：用本机 TCP 回环连接验证 `connect/accept/read/write` hook 能通过 epoll 恢复协程。

同时补了一个小工具函数 `sumSchedulerStats()`，减少测试里重复聚合调度器统计的代码。

## 7. 业务链路验证

新增 `scripts/docker_e2e.sh`，覆盖主业务链路：

- 注册。
- 登录。
- 直传上传。
- 分片上传。
- 文件列表。
- 下载响应头。
- 通过 Nginx + FastDFS 完整下载并比对内容。

这样可以确认调度器改造没有破坏业务主流程。

## 8. 配置增强

新增服务线程数配置：

- `config.txt`
- `docker/config.docker.yml`
- `FiberServer/my/application.cpp`
- `docker-compose.dev.yml`

支持：

```yaml
server:
  worker_threads: 5
```

也支持环境变量覆盖：

```bash
FIBER_WORKER_THREADS=2
```

这个能力用于压测不同线程数下的调度器表现。已验证 `FIBER_WORKER_THREADS=2` 生效，`/api/status` 能看到对应数量的 Processor。

## 9. 源码清理和稳定性修复

除了 GMP 主线，还做了几处小修：

- `FiberServer/base/mutex.h`：修正 `ScopedLockImpl` 析构逻辑，避免手动提前 `unlock()` 后析构时二次 unlock。
- `Scheduler::popGlobalTask()`：改成作用域管理全局锁，减少手动 unlock 带来的风险。
- 删除误拼写且未使用的空文件 `FiberServer/servlets/statuc_servlet.h`。
- 稳定 `test_iomanager_socket_hook()`，避免服务端过早关闭连接造成测试偶发失败。

## 10. 压测工具和结果

新增三类压测脚本：

- `scripts/docker_bench.sh`：轻量 GET 压测，默认测 `/api/status`。
- `scripts/docker_bench_business.sh`：自动创建测试用户和样例文件，压测 `status/login/myfiles/download/direct_upload`。
- `scripts/docker_bench_matrix.sh`：按请求数和并发数矩阵循环执行业务压测。

当前 Docker/本机环境下的压测结论：

| 场景 | 请求/并发 | 结果 |
| --- | ---: | --- |
| 纯 `/api/status` | 1000 / 100 | 1000 成功，0 错误，约 922 QPS，p95 约 113ms |
| 业务链路 | 100 / 10 | 全成功，约 734-821 QPS |
| 业务链路 | 200 / 20 | 全成功，约 677-850 QPS |
| 业务链路 | 300 / 30 | 全成功，约 787-872 QPS |
| 业务链路 | 500 / 50 | 全成功，约 640-831 QPS |
| 业务链路 | 800 / 80 | 全成功，约 743-854 QPS |
| 业务链路 | 1000 / 100 | `login` 和 `download` 各 1 个超时，开始进入边缘状态 |

所以当前环境下可以认为：

```text
业务读接口稳定档位约为 80 并发，读接口吞吐约 740-850 QPS。
100 并发开始出现偶发 15 秒级超时，不能算完全稳定档位。
```

压测后 `/api/status` 健康检查正常，调度器队列清空，没有观察到任务积压。

## 11. 已验证结果

容器内编译和测试已通过，覆盖：

- GMP 调度器基础执行。
- 全局队列批量搬运。
- 本地队列执行。
- 批量 work stealing。
- sleep/timer hook。
- socket IO hook。
- HTTP `/api/status`。
- 注册、登录、上传、文件列表、下载主链路。
- 多档并发业务压测。

最后一次健康检查返回：

```text
HTTP/1.1 200 OK
global_queue_size = 0
```

说明压测后服务仍可响应，调度队列没有残留积压。

## 12. 目前还不是完整 Go GMP 的部分

这次实现是简化版 GMP，不包括：

- Go runtime 级别的抢占式调度。
- GC safepoint 和 GC 协作。
- 系统调用阻塞时完整的 M/P 解绑。
- 完整 Go netpoller 状态机。
- 跨平台 IOCP/kqueue。
- 复杂优先级调度。

这些不是当前云存储项目必须实现的内容。当前版本的重点是让项目具备一个可讲清楚、可运行、可观测、可压测的 M:N 协程调度模型。

## 13. 后续可提升方向

后续如果继续优化性能，建议优先看：

1. 为什么默认 5 个 worker 时 `/api/status` 里第 5 个 Processor 没有执行统计。
2. work stealing 空转较多，`steal_fail_count` 偏高，可以考虑自适应退避。
3. 当前 HTTP 响应使用 `connection: close`，支持 keep-alive 后短请求吞吐可能明显提升。
4. 100 并发时 `login/download` 偶发 15 秒超时，需要定位 MySQL、FastDFS、Nginx 或 hook 覆盖是否存在阻塞点。
5. 增加业务耗时指标，拆分 HTTP、DB、FastDFS、调度器等待时间。

这些属于下一轮优化，不影响当前简化 GMP 版本的完整性。
