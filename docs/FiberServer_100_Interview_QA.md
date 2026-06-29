# FiberServer 项目面试 100 问（结合代码版）

> 使用方式：答案按面试口述风格编写。不要逐字背诵，重点记住“设计动机—代码实现—验证结果—现存边界”四层逻辑。
>
> 表述边界：本项目实现的是借鉴 Go GMP 思想的简化版 C++ M:N 协程调度器，不是完整复刻 Go runtime；业务层当前定位为轻量级 CI/CD 构建制品仓库，不是普通网盘，也不是完整实现 Artifactory/Nexus。性能数据要说明环境、脚本和样本量，不能把短样本压测包装成生产 SLA。

## 一、项目架构与个人工作（1—10）

### 1. 请你用一分钟介绍一下 FiberServer 项目。

**参考回答：**

FiberServer 是一个基于 C++17 的高并发 CI/CD 构建制品仓库。底层用 `Fiber`、`Scheduler`、`IOManager` 和 `hook.cpp` 实现用户态协程调度及 epoll 事件驱动；调度模型借鉴 Go GMP，引入 `Processor` 本地队列、全局队列、批量搬运和 work stealing。业务层围绕 `/api/artifacts/*` 提供项目 token、上传预检、checksum 去重、直传/分片上传、制品列表、latest/versions/builds 查询、下载和删除。文件内容存放在 FastDFS，制品元数据存放在 MySQL 的 `artifact_info`，物理文件去重和引用计数复用 `file_shared`，数据库访问使用 SOCI，并通过独立 `DbExecutor` 避免同步 SQL 阻塞 HTTP 工作线程。Nginx 负责反向代理、分片请求体落盘和下载转发。

### 2. 这个项目主要解决什么问题？为什么不直接写成普通线程池文件服务器？

**参考回答：**

项目重点不是做一个普通文件 CRUD，而是用“构建制品仓库”这个更具体的 IO 密集型场景验证少量 OS 线程承载大量并发连接。普通线程池遇到 `accept/read/write/connect/sleep` 等阻塞调用时，工作线程会被占住；FiberServer 通过 hook 把这些等待转换为 `IOManager::addEvent()` 加协程挂起，fd 就绪后再调度原协程。这样业务代码仍以同步方式书写，但底层等待是事件驱动的。数据库是同步库，所以又用 `DbExecutor` 隔离到独立 DB 调度器，避免 SQL 把 HTTP worker 占满。选择制品仓库，是因为它比网盘式文件服务更强调版本、构建号、checksum、不可变坐标和 CI 自动化。

### 3. 项目的整体架构可以分成哪几层？

**参考回答：**

我通常分成五层：第一层是基础设施，包括日志、配置、锁和工具函数；第二层是运行时，包括 `Fiber`、`Thread`、`Scheduler`、`IOManager`、`TimerManager` 和 syscall hook；第三层是网络协议，包括 `Socket`、`TcpServer`、HTTP parser、`HttpSession` 和 `HttpServer`；第四层是制品仓库 servlet，包括 token、precheck、direct/chunk upload、list、download、delete、latest、versions 和 builds；第五层是外部资源层，包括 `DbExecutor + SociManager + MySQL`、`FastDFSManager` 和 Nginx。Docker Compose 把 MySQL、FastDFS tracker/storage、Nginx 和应用组成可复现环境。

### 4. 一个 HTTP 请求从进入服务到返回，代码路径是什么？

**参考回答：**

`Application::run()` 创建一个 accept `IOManager` 和一个 HTTP `IOManager`。监听 socket 由 `TcpServer::startAccept()` 接收，连接通过 `m_ioWorker->scheduleGlobal()` 投递到 HTTP 调度器。`HttpServer::handleClient()` 创建 `HttpSession`，`recvRequest()` 读取并解析请求，然后 `ServletDispatch::getMatchedServlet()` 找到具体 servlet。业务需要查库时调用 `DbExecutor::submit()`：当前 HTTP fiber 挂起，SQL 在 DB 调度器执行，完成后把原 fiber 调度回来。最后 servlet 填充 `HttpResponse`，由 `HttpSession::sendResponse()` 写回；keep-alive 开启时继续读取同一连接上的下一个请求。

### 5. 你在这个项目里主要负责了什么？

**参考回答：**

我主要做了四部分：一是把原来只有全局队列的协程调度器改造成简化 GMP 模型，增加 `Processor`、本地队列、全局批量搬运、任务窃取和运行统计；二是保证新调度器与 `IOManager + epoll + hook` 的唤醒链路兼容；三是把业务层从普通文件服务收敛为 CI/CD 构建制品仓库，补 `artifact_info`、项目 token、不可变坐标、checksum 去重、制品查询和下载转发；四是补 Docker 环境、单元测试、E2E 和 artifact 专用压测脚本，并根据 `pool_wait_ms`、`db_ms`、P95/P99 等数据定位瓶颈。面试时我会明确哪些是当前已实现的，哪些只是后续优化方向。

### 6. 为什么项目使用 C++17？

**参考回答：**

运行时部分需要直接控制线程、fd、epoll、栈式协程和第三方 C 客户端，C++ 比较适合这种系统编程场景。代码里使用了 C++17 的 `std::invoke_result`、`std::optional`、智能指针和 lambda，例如 `DbExecutor::submit()` 用模板推导任务返回值，用 `optional<Result>` 跨线程保存结果。协程上下文没有使用 C++20 coroutine，而是 Boost.Context，因为这个项目采用的是有独立栈的 stackful fiber，便于把原有同步调用栈整体挂起。

### 7. 为什么说它只是“简化版 GMP”，不能说完整实现了 Go GMP？

**参考回答：**

对应关系确实存在：`Fiber` 类似 G，OS `Thread` 类似 M，`Processor` 类似 P；每个 P 有本地队列，还有全局队列和 work stealing，`IOManager + epoll` 类似简化 netpoll。但当前 M 与 P 是线程运行期间固定绑定的，没有 Go runtime 的抢占式调度、sysmon、GC safepoint、M/P 在阻塞系统调用时解绑重绑、runnext 等机制。因此准确说法是“借鉴 GMP 的 M:N 调度思想”，不是“复刻 Go runtime”。

### 8. 项目各模块之间是怎么解耦的？

**参考回答：**

`Scheduler` 只负责可运行任务，`IOManager` 通过继承 `Scheduler` 和 `TimerManager` 把 fd 事件、定时器转换成可调度任务；hook 层只负责把阻塞语义转换为 `addEvent + YieldToHold`。HTTP 层通过 `Servlet` 接口隔离路由与业务。业务层不直接管理数据库线程，而是通过 `DbExecutor::submit()` 提交 lambda；不直接管理 FastDFS 连接，而是通过 `FastDFSUtil/FastDFSManager` 获取连接。这样调度、协议、业务和存储的边界基本清楚。

### 9. 项目的运行参数如何配置？

**参考回答：**

默认从 `docker/config.docker.yml` 加载，`Application::init()` 也支持用 `FIBER_CONFIG` 指定配置文件。关键参数包括 `fiber.stack_size=131072`、`server.worker_threads=5`、`db.worker_threads=20`、MySQL 每个命名池的连接上限、`uploadfiles.chunk_sizes=5 MiB` 和 FastDFS 客户端配置路径。压测时还可以用 `FIBER_WORKER_THREADS`、`FIBER_DB_WORKER_THREADS` 和 `FIBER_PERF_LOG` 覆盖部分参数，便于不修改配置文件做对照实验。

### 10. 你认为项目当前最大的工程边界是什么？

**参考回答：**

它更接近学习和实验型高并发服务，还不是生产级制品仓库。主要边界包括：项目 token 只是轻量鉴权，不是完整 RBAC；`POST /api/artifacts/token` 更适合演示和 e2e，生产环境应放到管理员控制面或运维预置；上传完成后缺少服务端 checksum 复核；FastDFS 与 MySQL 之间没有分布式事务，失败补偿不完整；HTTP parser 对流水线请求支持有限；调度器没有抢占，CPU 密集 fiber 可能长期占用线程；压测时间和大文件样本还不够长。主动说清这些边界，比把项目包装成生产系统更可信。

## 二、Fiber 与简化 GMP 调度器（11—30）

### 11. 项目里的 G、M、P 分别对应什么？

**参考回答：**

G 对应 `Fiber`，它保存回调、状态和 Boost.Context 上下文；M 对应 `Thread`，也就是实际执行代码的 OS 线程；P 对应 `Scheduler::Processor`，它持有本地 `run_queue` 和调度统计。`Scheduler::bindProcessor()` 用轮询游标给进入 `run()` 的工作线程绑定 P，并用 `thread_local Processor* t_processor` 保存绑定关系。

### 12. Fiber 的上下文是如何实现和切换的？

**参考回答：**

当前实现使用 Boost.Context。`Fiber::makeContext()` 用 `boost::context::fixedsize_stack` 创建固定大小栈，把任务包装成一个 `boost::context::fiber`；`m_ctx` 保存任务侧上下文，`m_caller` 保存调用方上下文。`swapIn()` 通过 `std::move(m_ctx).resume()` 进入任务，`swapOut()` 通过 `std::move(m_caller).resume()` 回到调度协程。Boost.Context 的 fiber 是 move-only，所以每次 resume 后都要接住返回的新上下文对象。

### 13. Fiber 有哪些状态？状态如何流转？

**参考回答：**

状态有 `INIT、HOLD、EXEC、TERM、READY、EXCEPT`。新任务是 `INIT`，`swapIn()` 前变成 `EXEC`；主动 `YieldToReady()` 时变成 `READY`，回到调度器后会再次入队；等待 IO、定时器或 DB 时走 `YieldToHold()`，切回后由调度器标成 `HOLD`，等待外部事件重新 schedule；回调正常结束变成 `TERM`，抛异常变成 `EXCEPT`。`Fiber::reset()` 只允许对 `INIT/TERM/EXCEPT` 的任务 fiber 重建上下文。

### 14. `YieldToReady()` 和 `YieldToHold()` 有什么区别？

**参考回答：**

`YieldToReady()` 表示“我现在让出 CPU，但仍然可运行”。它在切出前把状态设为 `READY`，`Scheduler::run()` 看到后立即重新 `schedule()`。`YieldToHold()` 表示“我在等外部条件，不应马上重跑”，例如 fd 就绪、定时器到期或 DB 任务完成；它只切出，不主动重新入队，之后由 `IOManager::triggerEvent()`、timer 回调或 `DbExecutor` 完成回调唤醒。

### 15. 为什么 `YieldToHold()` 不在 `swapOut()` 前直接设置 `HOLD`？

**参考回答：**

这是为了缩小并发唤醒竞态。若 fiber 还没真正切出就先变成 `HOLD`，另一个线程可能收到 IO 事件并重新调度它，导致同一个执行上下文被两个线程同时 resume。当前代码让它在切出期间保持 `EXEC`，回到 `Scheduler::run()` 后再设为 `HOLD`。同时取任务逻辑会跳过 `EXEC` fiber。更严格的生产实现会用原子的 parked/wakeup 状态机解决“先唤醒还是先挂起”的竞争，而不能只依赖普通枚举状态。

### 16. `Scheduler` 的 `use_caller` 参数有什么作用？项目实际怎么用？

**参考回答：**

`use_caller=true` 时，创建调度器的线程也能作为一个执行线程，构造函数会创建绑定 `run()` 的 `m_rootFiber`，`stop()` 时通过 `call()` 让调用线程参与收尾。`use_caller=false` 时所有任务只在新建工作线程执行。当前 `Application` 创建 accept、HTTP 和 DB 三类 `IOManager` 时都传 `false`，这样主线程只负责应用生命周期，不参与业务调度，行为更容易控制。

### 17. M 和 P 是如何绑定的？会动态解绑吗？

**参考回答：**

每个工作线程进入 `Scheduler::run()` 后调用 `bindProcessor()`，通过 `m_nextProcessor++ % m_processors.size()` 选择一个 P，并放入 thread-local 的 `t_processor`。本轮 `run()` 生命周期内绑定保持不变，退出时清空。当前没有实现 Go 那种线程进入阻塞 syscall 后释放 P、再由其他 M 接管 P 的机制，负载不均主要靠全局队列搬运和 work stealing 修正。

### 18. `schedule()` 如何决定任务进入本地队列还是全局队列？

**参考回答：**

`Scheduler::scheduleNoLock()` 会检查三个条件：任务没有指定线程、调用线程当前就在这个 Scheduler 中、并且存在属于该 Scheduler 的 `t_processor`。满足时进入当前 P 的 `run_queue`；否则进入全局 `m_fibers`。因此外部线程提交的首轮任务进入全局队列，而 fiber 在工作线程里 `YieldToReady()`、timer 或 IO 回调恢复时通常进入本地队列，提升局部性并减少全局锁竞争。

### 19. 为什么还要提供 `scheduleGlobal()`？

**参考回答：**

它用于明确要求任务走全局入口，避免调用位置变化后意外落到某个 P 的本地队列。当前 `TcpServer::startAccept()` 把新连接的 `handleClient` 用 `m_ioWorker->scheduleGlobal()` 投递，使连接任务可被 HTTP worker 中任意 P 获取。虽然 accept worker 和 HTTP worker 当前是不同 Scheduler，普通 `schedule()` 也会走全局队列，但显式调用表达了分发语义，也避免未来合并调度器后连接集中到一个本地队列。

### 20. 调度器为什么按“本地队列→全局队列→偷取→idle”的顺序取任务？

**参考回答：**

本地优先让刚恢复或刚 yield 的 fiber 尽量留在原 P，减少全局锁和缓存抖动；本地为空后检查全局队列，保证外部提交任务能进入系统；两者都没有任务时再从其他 P 偷取，解决局部积压；最后才进入 idle。这个顺序也有边界：如果某个 P 的本地任务持续不断，全局任务公平性可能受影响，生产版可以增加执行配额，例如每执行若干本地任务强制检查一次全局队列。

### 21. 全局队列为什么采用批量搬运？

**参考回答：**

`popGlobalTask()` 在拿到第一个立即执行的任务后，会把全局队列当前可见任务的大约一半搬到当前 P。本质上是用一次全局锁竞争换一批本地任务，摊薄后续取任务成本；但又不全部拿走，避免一个 P 独占全局任务。指定线程任务不会被搬入本地队列，`EXEC` 状态的 fiber 也不会被移动。搬运数量和批次分别记录在 `global_pull_count`、`global_batch_count`。

### 22. work stealing 是怎么实现的？

**参考回答：**

`stealTask()` 用 `m_nextStealProcessor` 轮转选择起始 victim，避免所有空闲线程总盯着同一个 P。victim 自己从队头消费，偷取方从队尾取大约一半普通任务，降低两端操作冲突。偷到的第一个任务立即执行，剩余任务追加到当前 P。本实现不会偷指定线程任务和 `EXEC` fiber，并记录尝试、失败、成功批次和任务数量，便于判断是否存在大量无效偷取。

### 23. 调度器如何控制锁竞争和死锁风险？

**参考回答：**

全局队列由 `Scheduler::m_mutex` 保护，每个 P 的本地队列有独立 `Processor::mutex`，因此不同 P 的本地操作不会竞争同一把锁。全局批量搬运时先在全局锁下摘任务，释放全局锁后再锁当前 P；偷取时先锁 victim，释放后再锁 current，避免同时持有两个 P 的锁。`getStats()` 按全局锁再逐个 P 锁读取快照，相关路径要保持一致锁序。统计计数使用原子变量，队列长度仍在对应锁下读取。

### 24. 指定线程执行的任务如何保证语义？

**参考回答：**

`FiberAndThread::thread != -1` 表示指定 OS 线程。此类任务始终放全局队列，`popGlobalTask()` 如果发现目标线程不是当前线程，就保留任务并设置 `tickle_me` 唤醒其他 worker；批量搬运和偷取也会跳过指定线程任务。这样不会因为本地队列优化破坏线程亲和语义。代价是目标线程忙时任务会一直留在全局队列，所以它更适合少量特殊任务。

### 25. 为什么 callback 任务还要封装成 Fiber？

**参考回答：**

Scheduler 同时接受已有 `Fiber` 和普通 `std::function<void()>`。普通 callback 进入执行阶段时会被包装进 `cb_fiber`，这样 callback 内也能使用 `YieldToReady/YieldToHold` 和 hook。`run()` 会复用已经终止或异常的 `cb_fiber`，通过 `reset()` 替换回调并重建上下文，减少频繁分配协程对象和栈的成本。

### 26. `tickle()` 是做什么的？为什么不能只靠轮询？

**参考回答：**

`tickle()` 用来唤醒已经进入 idle 的工作线程。基础 `Scheduler::tickle()` 只有日志行为，真正用于网络服务的是 `IOManager::tickle()`：如果存在 idle 线程，就向 `m_tickleFds[1]` 写一个字节，使阻塞在 `epoll_wait()` 的线程从管道读事件中醒来。新任务、新的更早定时器、指定线程任务未被当前线程消费时都可能需要 tickle。只靠固定周期轮询会增加任务唤醒延迟或空转 CPU。

### 27. 调度器如何优雅停止？

**参考回答：**

`Scheduler::stop()` 设置 `m_autoStop` 和 `m_stopping`，tickle 所有工作线程，再等待线程 join。真正退出条件在 `stopping()`：没有活跃线程、全局队列为空、所有 P 的本地队列也为空。`IOManager::stopping()` 还要求没有最近定时器且 `m_pendingEventCount==0`。这是 GMP 改造必须同步修改的地方，否则只检查旧的全局队列可能在本地队列还有任务时提前退出。

### 28. `/api/status` 为什么要暴露 P 级统计？

**参考回答：**

只看 QPS 无法证明本地队列和偷取逻辑真的工作。`Scheduler::getStats()` 暴露每个 P 的 `queue_size`、本地投递数、local/global/steal 三类执行数、全局搬运和 steal 的批次及失败次数。`StatusServlet` 把它们输出成 JSON。压测后可以检查 `global_queue_size` 是否归零、任务是否命中本地队列、是否存在过多偷取失败。它是带锁采样的观测快照，不是暂停所有线程后得到的严格一致状态。

### 29. GMP 调度器是怎么测试的？

**参考回答：**

`tests/test.cpp` 有两组核心测试。`test_gmp_scheduler()` 从外部提交 200 个任务，每个任务先执行一次、`YieldToReady()` 后再执行一次，验证首轮全局执行和恢复后的本地调度统计。`test_gmp_batch_stealing()` 让一个父任务向单个 P 的本地队列塞 200 个相互等待的子任务；如果其他 P 不偷取，测试会卡住，因此最终还断言 `stolen`、`steal_batches`、`steal_executed` 都大于零。

### 30. 当前调度器还有哪些可以继续优化的点？

**参考回答：**

第一是增加公平性配额，避免本地队列长期压制全局队列；第二是用更严格的原子 parked/wakeup 协议处理挂起与唤醒竞态；第三是减少链表和互斥锁开销，可考虑有界环形队列或无锁队列；第四是增加抢占或至少对长时间运行任务做监测；第五是根据 steal 失败率做退避，避免空闲线程频繁扫描所有 P；第六是完善 M/P 解绑与阻塞任务隔离。当前版本优先保证模型可理解、可验证，没有追求 Go runtime 的完整复杂度。

## 三、IOManager、epoll 与 Hook（31—45）

### 31. `IOManager` 为什么同时继承 `Scheduler` 和 `TimerManager`？

**参考回答：**

网络 IO 和定时器最终都要把等待中的 fiber 重新变成可运行任务。继承 `Scheduler` 后，IO 事件可以直接调用 `schedule()`；继承 `TimerManager` 后，`idle()` 可以用最近定时器决定 `epoll_wait()` 超时，并把到期回调批量加入调度器。这样线程无任务时只需要阻塞在一个事件循环里，同时等待 fd、tickle 管道和定时器。

### 32. IOManager 如何保存每个 fd 的事件状态？

**参考回答：**

`m_fdContexts` 以 fd 为下标保存 `FdContext*`。每个 `FdContext` 有 READ、WRITE 位掩码、独立互斥锁，以及两个 `EventContext`；事件上下文保存原 Scheduler 和待唤醒的 fiber 或 callback。`addEvent()` 在 fd 超出当前数组时扩容，然后用 `EPOLL_CTL_ADD/MOD` 注册事件。数组整体由读写锁保护，单个 fd 状态由自己的 mutex 保护，避免所有 fd 操作都竞争一把锁。

### 33. `addEvent()` 注册事件时为什么既支持 fiber 又支持 callback？

**参考回答：**

如果调用方传 callback，事件就绪后直接调度 callback；如果没传，`addEvent()` 会保存当前 `Fiber::GetThis()`，并断言它处于 `EXEC`。hook 的同步式写法主要使用后者：当前 fiber 注册 READ/WRITE 后挂起，事件到来恢复原调用栈。框架内部也可以直接注册 callback，避免为了简单事件先创建业务 fiber。

### 34. fd 就绪后，fiber 是怎么被唤醒的？

**参考回答：**

`IOManager::idle()` 从 `epoll_wait()` 得到事件后，计算真正发生的 READ/WRITE，先用 `EPOLL_CTL_MOD/DEL` 更新监听，再调用 `FdContext::triggerEvent()`。该函数清除事件位，把保存的 callback 或 fiber 通过原 `Scheduler` 的 `schedule()` 重新入队，并清空上下文。之后 `m_pendingEventCount` 减一，某个工作线程取到该 fiber 后从之前的系统调用位置继续执行。

### 35. 项目为什么使用 epoll ET？使用时有什么要求？

**参考回答：**

注册 fd 时统一带 `EPOLLET`，减少同一就绪状态反复通知。ET 的前提是底层 fd 必须非阻塞，并且上层要持续处理到 `EAGAIN`。`FdCtx::init()` 会用原始 `fcntl_f` 给 socket 设置系统级 `O_NONBLOCK`；`do_io()` 在返回 `EAGAIN` 时才注册事件并挂起。HTTP 层的解析循环、`readFixSize/writeFixSize` 会持续读写所需数据。若新增业务只读一次就长期不再读，ET 下仍可能留下未消费数据，因此必须遵守这一约束。

### 36. `IOManager::idle()` 的事件循环具体做了什么？

**参考回答：**

每轮先调用 `stopping(next_timeout)`，如果不能退出，就把最近定时器时间与最大 3000ms 取较小值作为 `epoll_wait()` 超时。返回后通过 `listExpiredCb()` 提取到期 timer 并 schedule；再遍历最多 1024 个 epoll 事件，处理 tickle 管道、错误/挂断事件及普通读写事件。最后 idle fiber `swapOut()` 回到 Scheduler，让运行循环优先处理刚刚加入的任务。

### 37. 为什么 tickle 管道也要设置成 ET 和非阻塞？

**参考回答：**

tickle 的目的只是把 `epoll_wait()` 唤醒，不应让写管道或读管道本身阻塞工作线程。构造函数把读端设为 `O_NONBLOCK`，以 `EPOLLIN|EPOLLET` 注册；事件发生后用 256 字节缓冲区循环读到没有数据，满足 ET 必须排空的要求。`IOManager::tickle()` 只有检测到 idle 线程时才写一个字节，减少无效系统调用。

### 38. hook 是怎么替换系统调用的？

**参考回答：**

`hook.cpp` 用 `dlsym(RTLD_NEXT, "read")` 等方式保存 libc 原函数地址，并在 `extern "C"` 下定义同名函数，从动态链接层拦截调用。每个调度工作线程进入 `Scheduler::run()` 时调用 `set_hook_enable(true)`；非调度线程的 thread-local 标志仍为 false，会直接执行原函数。当前覆盖 `sleep/usleep/nanosleep/socket/connect/accept/read/write/recv/send/close/fcntl/ioctl/setsockopt` 等调用。

### 39. `do_io()` 如何把一次阻塞 read/write 变成异步等待？

**参考回答：**

它先判断 hook 是否开启、fd 是否由 `FdManager` 管理、是否为 socket、用户是否主动要求非阻塞。满足协程化条件后先调用原始函数；`EINTR` 就重试，成功就直接返回；若是 `EAGAIN`，就在当前 `IOManager` 注册 READ 或 WRITE，按 fd 的收发超时添加条件定时器，然后 `YieldToHold()`。事件或超时唤醒后取消 timer：超时则设置 `errno=ETIMEDOUT`，否则跳回 retry 再调用一次原函数。

### 40. IO 超时如何避免“事件就绪”和“定时器到期”重复唤醒？

**参考回答：**

`do_io()` 创建共享的 `timer_info`，定时器只持有 weak_ptr。超时回调先检查对象是否还存在以及 `cancelled` 是否已设置，再写入 `ETIMEDOUT` 并调用 `cancelEvent()`；正常 IO 唤醒后会取消 timer。`addConditionTimer()` 也通过 weak condition 保证等待对象销毁后不执行回调。这个设计减少重复回调，但挂起/唤醒本身仍是并发敏感区，需要靠调度器状态和回归测试共同保证。

### 41. `connect()` 为什么不能完全复用普通 `do_io()`？

**参考回答：**

非阻塞 connect 的中间状态是返回 `-1/EINPROGRESS`，不是普通读写的 `EAGAIN`。`connect_with_timeout()` 因此单独监听 WRITE，超时逻辑唤醒后还必须用 `getsockopt(SOL_SOCKET, SO_ERROR)` 判断连接到底成功还是失败。默认连接超时来自 `tcp.connect.timeout`，当前 Docker 配置为 5000ms，并支持配置监听器动态更新内部值。

### 42. 为什么要区分系统非阻塞和用户非阻塞？

**参考回答：**

协程框架必须把 socket 的内核状态设为非阻塞，否则原始 read 可能直接卡住整个 OS 线程；但业务如果没有主动设置 `O_NONBLOCK`，仍希望看到传统阻塞语义。`FdCtx` 因此保存 `m_sysNonblock` 和 `m_userNonblock`。hook 的 `F_GETFL` 会隐藏框架内部的非阻塞位，`F_SETFL/ioctl(FIONBIO)` 记录用户意图；一旦用户明确要求非阻塞，`do_io()` 就直接返回原始结果，不再替用户挂起 fiber。

### 43. hook `close()` 时为什么要先 `cancelAll()`？

**参考回答：**

一个 fd 关闭时可能仍有 fiber 等待它的 READ 或 WRITE。如果只调用系统 close，等待上下文可能一直留在 epoll 和 `m_fdContexts` 中，甚至 fd 号被复用后误唤醒旧任务。hook `close()` 先通过当前 `IOManager` 触发并取消该 fd 的全部事件，再从 `FdManager` 删除上下文，最后调用原始 `close_f()`，把生命周期状态一起清理掉。

### 44. `sleep/usleep/nanosleep` 是怎么协程化的？

**参考回答：**

hook 开启时不调用内核阻塞睡眠，而是获取当前 fiber 和 `IOManager`，添加相应毫秒数的 timer；timer 回调把该 fiber 重新 schedule，然后当前 fiber `YieldToHold()`。这样一个协程 sleep 时，OS 线程可以执行其他任务。`test_iomanager_sleep_timer()` 验证 sleep 前后计数都执行，并检查 timer 恢复路径确实进入了 Scheduler 统计。

### 45. 如何证明 socket hook 没被 GMP 改造破坏？

**参考回答：**

`test_iomanager_socket_hook()` 在一个两线程 `IOManager` 里同时运行本地 server 和 client，覆盖 `socket/connect/accept/read/write/close`。client 发送 `ping`，server 返回 `pong`，最后断言双方状态和内容正确，并检查本地调度及总执行计数。这个测试不是完整网络测试，但能验证最关键的“系统调用返回 EAGAIN—注册 epoll—fiber 挂起—事件唤醒—继续执行”闭环。

## 四、HTTP 服务链路（46—55）

### 46. 应用里有哪几类线程池？为什么要拆开？

**参考回答：**

当前有三个独立 `IOManager`：accept 调度器固定 1 个线程；HTTP 调度器默认 5 个线程；`DbExecutor` 的 DB 调度器默认 20 个线程。accept 单独拆出可以避免处理业务时延迟接收新连接；HTTP worker 专注协议和业务协程；同步 SQL 即使阻塞，也主要占 DB worker，不会直接耗尽 HTTP worker。线程数可以通过配置和环境变量分别调节。

### 47. 为什么 accept 线程不直接处理客户端请求？

**参考回答：**

`TcpServer::startAccept()` 的职责保持很小：循环 accept、设置接收超时、把 `handleClient` 投递给 `m_ioWorker`。如果 accept 线程自己解析 HTTP、查库或访问 FastDFS，一个慢请求就会降低新连接接入能力。当前代码通过独立 `m_acceptWorker` 和 `scheduleGlobal()` 把接入与连接处理分开，类似 Reactor 中 acceptor 和 worker 的职责划分。

### 48. HTTP keep-alive 在代码里如何实现？

**参考回答：**

`HttpServer` 构造时传入 `keepalive=true`。`handleClient()` 用 do-while 循环读取请求、构造响应并发送；只要服务端允许 keep-alive 且请求没有 `Connection: close`，就继续在同一个 `HttpSession` 读取下一次请求。Nginx upstream 也配置了 `keepalive 256`、`keepalive_requests 10000` 和 HTTP/1.1，并清空代理侧 `Connection` 头，从而复用到 FiberServer 的连接。

### 49. keep-alive 为什么能明显提升这个项目的 QPS？

**参考回答：**

状态查询、制品列表、latest/versions/builds 和下载定位都是短请求，短连接模式下 TCP 建连、关闭和 accept 调度占比很高。历史压测里 keep-alive 对短读接口有明显收益；现在 artifact 专用压测脚本也默认开启 `BENCH_KEEPALIVE=1`，用于减少连接建立成本，让结果更接近服务端处理能力。面试时可以说连接复用对短请求收益明显，但不要把历史 login/myfiles 数据当作当前公开接口数据。

### 50. `HttpSession::recvRequest()` 如何处理半包和请求体？

**参考回答：**

它先按配置的 request buffer 循环 read，把新数据接在 `offset` 后，由 `HttpRequestParser::execute()` 消费已解析字节，直到头部完成。然后读取 `Content-Length`，先把解析缓冲区里剩余的部分拷贝到 body，不足部分再通过 `readFixSize()` 补齐。当前实现每个请求新建 parser 和缓冲区，对 HTTP pipelining 的“同一次 read 已包含下一个请求”处理不完整，因此它适合普通 keep-alive 串行请求，不应宣称完整支持 HTTP/1.1 流水线。

### 51. Servlet 路由是怎么实现的？

**参考回答：**

`HttpServer` 构造时创建 `ServletDispatch`，当前只注册 `/api/status` 和 `/api/artifacts/*` 这组公开路由，例如 `precheck`、`upload/direct`、`upload/chunk`、`list`、`download`、`delete`、`latest`、`versions`、`builds` 和 `token`。`ServletDispatch` 用读写锁保护精确路由 map 和 glob 列表；查找时先匹配精确路径，再用 `fnmatch()` 遍历 glob，最后返回 `NotFoundServlet`。旧的 `/api/login`、`/api/register` 和普通文件接口已经移除，e2e 会验证这些路径返回 404。

### 52. 为什么下载接口使用 `addGlobServlet()`？

**参考回答：**

当前下载入口是 `/api/artifacts/download`，参数来自 query 中的 `project_name/version/build_no/artifact_name`。代码仍用 `addGlobServlet()` 注册这个下载入口，实际匹配效果接近精确路由；保留 glob 主要是沿用现有分发器能力，后续如果扩展路径式下载不需要改调度层。面试中我不会夸大成已经实现复杂 REST 路径参数，当前制品坐标仍主要来自 query。

### 53. 为什么客户端连接任务使用 `scheduleGlobal()`？

**参考回答：**

连接由 accept Scheduler 接收，但处理发生在 HTTP Scheduler。强制走 HTTP Scheduler 的全局队列，任意 HTTP P 都可以拿到新连接，避免新连接天然集中到某个本地队列。`popGlobalTask()` 还会批量把连接任务搬到当前 P，降低后续全局锁竞争。对跨 Scheduler 投递来说这也是清晰的边界：acceptor 只生产连接任务，worker 池负责消费。

### 54. `/api/status` 在故障排查中能回答哪些问题？

**参考回答：**

它能看到当前 Scheduler 名称、全局积压、活跃和空闲线程数，以及每个 P 的队列与任务来源统计。比如压测结束后 `global_queue_size` 不归零，说明还有积压或停止逻辑异常；`local_execute_count` 一直为零，说明本地恢复路径没生效；`steal_attempt_count` 很高但 `steal_count` 很低，说明空转偷取严重。但它看不到 fd 数量、timer 数量、DB 池等待者和请求延迟，生产化时还需要 Prometheus 指标补齐。

### 55. 项目 token 和接口鉴权达到生产要求了吗？

**参考回答：**

没有达到生产级。当前旧登录注册接口已经移除，写接口使用项目 token：`Authorization: Bearer <token>` 或 `X-Artifact-Token`。服务端只保存 token 哈希，能防止一个项目的 CI token 写入另一个项目。但这仍不是完整权限系统：token 创建接口目前用于演示和 e2e，缺少管理员控制面、过期轮换策略、审计日志、细粒度 RBAC、TLS、防重放和限流。读接口当前也没有完整鉴权，生产版需要统一权限中间件。

## 五、FastDFS、checksum 去重、分片上传与 Nginx（56—70）

### 56. 为什么制品内容和元数据分别放在 FastDFS 与 MySQL？

**参考回答：**

FastDFS 擅长保存和分发大对象，MySQL 擅长查询制品坐标、checksum、版本、构建号和引用关系。代码中 FastDFS 返回 `file_id` 作为物理对象定位符；`artifact_info` 保存制品业务元数据，`file_shared` 保存 checksum 到物理 `file_id`、大小和引用计数，`file_info` 作为内部逻辑文件记录被上传链路复用。这样制品列表、latest/versions/builds 查询、checksum 命中判断和删除引用都可以通过 SQL 完成，而文件内容不进入关系数据库。

### 57. `/api/artifacts/precheck` 为什么只是预检，不直接接收文件？

**参考回答：**

预检先根据 `project_name/version/build_no/artifact_name` 判断制品坐标是否已存在。如果同坐标同 checksum，返回已存在或可复用；如果同坐标不同 checksum，返回冲突，避免同一个版本和构建号下的制品被静默覆盖。系统没有该制品时，再根据 size 和 `ChunkManager::getChunkSizes()` 决定直传还是分片，并返回已上传分片列表。这样 CI 客户端在发送大量数据前就能知道是否需要上传、采用哪种协议，以及是否存在坐标冲突。

### 58. 直传和分片上传的阈值如何计算？

**参考回答：**

阈值来自 `uploadfiles.chunk_sizes`，当前是 `5242880`，也就是 5 MiB。文件大小不超过阈值时 `/api/artifacts/precheck` 返回 direct；大于阈值时总分片数按 `(size + chunkSize - 1) / chunkSize` 向上取整，并创建 `<tmp_root>/<project_name>/<checksum>` 任务目录。阈值应结合 Nginx 落盘、磁盘吞吐、FastDFS 上传方式和网络环境压测，不是越大越好。

### 59. checksum 去重的数据模型是怎么设计的？

**参考回答：**

`file_shared.file_md5` 是唯一键，每条记录表示一份物理文件，保存 `file_id/file_size/ref_count`；`artifact_info` 表示一个制品坐标，唯一键是 `project_name + version + build_no + artifact_name`。新制品复用已有物理文件时，`UploadServlet` 在同一 SOCI 事务中执行 `file_shared::IncrementRef()`、创建内部 `file_info` 记录和 `artifact_info` 记录。删除制品时先删 `artifact_info` 和内部文件记录，再减少 `ref_count`；只有引用数变成 0 才删除 `file_shared` 和 FastDFS 物理文件。

### 60. 如何避免同一制品坐标被错误覆盖？

**参考回答：**

代码会先查 `artifact_info` 的制品坐标。如果同坐标已存在且 checksum 相同，就按幂等复用处理；如果 checksum 不同，就返回 `artifact checksum conflict`。这比普通文件名覆盖更适合 CI/CD，因为同一个版本、同一个 build 的产物应该是不可变的。并发场景下仍要依赖数据库唯一键兜底，把重复键错误转换为幂等成功或明确冲突，不能只靠应用层先查再插。

### 61. 小文件直传的完整链路是什么？

**参考回答：**

客户端先从 `/api/artifacts/precheck` 得到 direct，再把文件内容发到 `/api/artifacts/upload/direct`。`DirUploadServlet` 从 query 读取 `project_name/version/build_no/artifact_name/checksum/size/artifact_type`，从 body 取内容，调用 `FastDFSUtil::uploadSmallFile()` 得到 `file_id`；随后在 DB worker 中开启 SOCI 事务，同时创建内部 `file_info`、`file_shared` 和 `artifact_info`，成功后提交。请求还用 `ScopedPerfLog` 分别记录 `fastdfs_ms` 和 `db_ms`，方便判断慢在存储还是数据库。

### 62. 如果 FastDFS 上传成功，但写 MySQL 失败，会怎样？

**参考回答：**

当前会返回错误，但刚上传的 FastDFS 对象可能成为孤儿，因为 FastDFS 操作发生在 DB 事务之前，事务回滚不能回滚外部存储。分片最终上传也有同样问题。生产方案可以在 DB 中先写 `PENDING` 任务，上传成功后改为 `COMMITTED`；失败时投递带重试的补偿删除任务，或者定期扫描 FastDFS/上传任务表清理孤儿。直接把两者叫“强一致事务”是不准确的。

### 63. 分片上传的完整代码流程是什么？

**参考回答：**

预检创建任务目录并返回总分片数和已存在编号。每个分片请求进入 `/api/artifacts/upload/chunk`，`ChunkUploadServlet` 优先把 Nginx 临时文件移动到 `<project_name>/<checksum>/<index>`，也支持直接保存 body。`isAllChunksReady()` 判断数量齐全后，`mergeChunks()` 按编号升序合并成 `<task>/<checksum>`；再调用 `uploadBigFile()` 上传 FastDFS，在一个 SOCI 事务中写内部 `file_info`、`file_shared` 和 `artifact_info`，最后 `FSUtil::Rm()` 删除临时任务目录。

### 64. Nginx 在分片上传里具体做了什么？

**参考回答：**

`location = /api/artifacts/upload/chunk` 配置 `client_body_in_file_only on`，让 Nginx 把大请求体先写到共享目录；代理到 FiberServer 时关闭 body 转发，并通过 `X-File-Path` 传递临时文件路径。`ChunkManager::saveChunk()` 用 `rename()` 把临时文件移动到任务目录。这样应用不需要把每个大分片完整读入内存，减少内存峰值和一次用户态拷贝，但要求 Nginx 与应用共享同一文件系统或 volume。e2e 已覆盖 body 模式和 Nginx 落盘模式。

### 65. 断点续传是怎么实现的？

**参考回答：**

任务目录按 project_name 和 checksum 隔离，分片文件名就是 index。`FSUtil::GetIndices()` 遍历目录中的普通文件，把可转换为整数的文件名收集并排序；`/api/artifacts/precheck` 把这个数组返回给客户端，客户端只补传缺失 index。相同 index 再次上传时 `FSUtil::Mv()` 会先删除目标后 rename，因此具有覆盖式重试语义。

### 66. `isAllChunksReady()` 只比较数量是否安全？

**参考回答：**

不完全安全。当前只判断 `indices.size() == total_chunks`，没有明确验证索引恰好是 `0..total_chunks-1`，也没有检查每片大小。虽然同一目录下文件名天然唯一，异常编号例如 `0,1,99` 仍可能让数量相等并进入合并。更严格的实现应验证连续范围、非最后一片的固定大小、合并后的总大小，并最终计算服务端 checksum/MD5 与客户端声明值比较。

### 67. 分片合并如何保证顺序？还有什么完整性风险？

**参考回答：**

`FSUtil::GetIndices()` 会升序排序，`FSUtil::MergeFiles()` 按排序后的路径依次用 1 MiB 缓冲区读写，因此正常编号的合并顺序是确定的。风险在于当前合并后没有重新计算 checksum/MD5，也没有把实际合并大小与声明 size 做强校验；客户端或磁盘异常可能产生内容错误但仍写入 FastDFS。生产版应在上传物理存储前完成大小和摘要校验。

### 68. 未完成的分片任务如何清理？

**参考回答：**

`ChunkManager` 创建时在当前 `IOManager` 注册循环 timer，默认每 3600 秒运行 `cleanTask()`。它把最后修改时间早于 `now - uploadfiles.last_time` 的文件删除，默认保留 7200 秒，然后递归删除空任务目录。这个机制解决客户端中断留下的临时文件，但需要注意清理与正在上传之间的竞态，生产版最好给任务维护状态或租约，而不是只依赖文件 mtime。

### 69. 下载为什么不由 C++ 服务直接把文件内容读回来？

**参考回答：**

`DownloadServlet` 只在 DB worker 中根据 `project_name/version/build_no/artifact_name` 找到 `file_id`，然后返回 `X-Accel-Redirect: /<file_id>`。Nginx 的 `/group1/` location 被标记为 `internal`，收到内部跳转后代理到 FastDFS storage 的 HTTP 服务。这样元数据判断留在应用层，大文件传输交给 Nginx/FastDFS，避免长期占用 C++ fiber、应用缓冲区和业务带宽。当前读接口还没有完整鉴权，生产版需要补统一权限控制。

### 70. FastDFS 连接是如何管理的？

**参考回答：**

`FastDFSManager::get()` 优先从空闲 `m_conns` 取包装对象；没有时调用 `tracker_get_connection()` 并查询 storage。返回值是带自定义 deleter 的 `shared_ptr`，释放时归还空闲列表，空闲缓存超过 `m_maxConn=10` 才销毁。要注意这个 10 是空闲缓存上限，不是严格的并发连接总上限；列表为空时仍会继续创建。另一个关键配置是 FastDFS C 客户端的 `use_connection_pool=true`，它关系到协程并发下底层 `ConnectionInfo` 是否会被错误复用。

## 六、SOCI、数据库线程与一致性（71—80）

### 71. 为什么把原 MySQL C API 迁移到 SOCI？

**参考回答：**

主要目的是减少 `MYSQL_BIND`、结果缓冲区和列下标解析等样板代码，让参数绑定、行读取和事务边界更清晰。`mysqlop_soci.cpp` 使用 `soci::use/into/rowset/transaction` 实现制品元数据、项目 token、物理文件引用和内部文件记录查询，业务代码更容易审查。但 SOCI 仍然是同步访问库，迁移并不会自动变成异步，所以项目同时增加了 `DbExecutor` 和连接池等待协程化，不能把性能改善只归因于换库。

### 72. `DbExecutor::submit()` 是怎么避免 SQL 阻塞 HTTP worker 的？

**参考回答：**

调用方在 HTTP fiber 中把数据库 lambda 提交给独立 DB `IOManager`。`submit()` 保存调用方 Scheduler 和当前 fiber，把 lambda schedule 到 DB worker，然后当前 HTTP fiber `YieldToHold()`。DB lambda 完成后把结果或异常写入共享 `TaskState`，再调用 `caller->schedule(fiber)` 恢复原 HTTP fiber。对 servlet 来说调用形式仍像同步函数，但同步 SQL 实际占用的是 DB worker。

### 73. 为什么 `submit()` 检测到自己已在 DB Scheduler 时直接执行？

**参考回答：**

如果 DB 任务内部再次调用 `submit()`，继续投递后再挂起，可能出现 DB worker 等待自己队列中的子任务，线程数不足时形成自锁。代码通过 `Scheduler::GetThis() == m_iom.get()` 判断当前已在 DB 调度器，直接调用 `fn()`。非协程环境没有可安全挂起的 caller/fiber 时也回退为同步执行，保证接口不会错误地操作不存在的协程上下文。

### 74. DB 线程的返回值和异常如何传回原 fiber？

**参考回答：**

`TaskState` 内有 mutex、`std::optional<Result>` 和 `std::exception_ptr`。DB lambda 正常结束就 `emplace` 结果，异常则保存 `current_exception()`；两种情况最后都重新调度调用 fiber。调用 fiber 恢复后加锁读取状态，有异常就在原调用栈 `rethrow_exception()`，否则 move 返回结果。这样业务的 try/catch 语义不会因为跨线程执行而丢失。

### 75. SOCI 连接池的获取策略是什么？

**参考回答：**

`SociManager` 按配置名维护独立 `PoolState`。`get()` 先复用 idle 连接；没有空闲且 `totalCount < maxConn` 时先占名额，释放池锁后建立连接；达到上限时，在协程环境把当前 fiber 放进 FIFO waiter 链表并挂起，默认最多等 3000ms。连接归还时优先直接交给最早 waiter，没有等待者才放回 idle。建连不持有池锁，避免慢连接把整个池锁住。

### 76. 连接归还和等待超时同时发生时，如何避免重复唤醒？

**参考回答：**

每个 `Waiter` 有 `linked`、`done`、`timedOut` 和自己在链表中的 iterator。超时回调与 `freeSoci()` 都在同一个 `PoolState::mutex` 下检查和修改 `done`：先完成的一方把它设为 true，另一方看到后跳过。超时路径可用 iterator O(1) 从等待链表摘除；拿到连接的路径把连接直接写到 `waiter->db`。锁外才执行 `scheduler->schedule(fiber)`，避免唤醒后的代码反向竞争池锁。

### 77. 为什么连接池返回带自定义 deleter 的 `shared_ptr`？

**参考回答：**

业务只需要持有 `SociDB::ptr`，离开作用域时 deleter 自动调用 `freeSoci(name, db)`，不容易遗漏显式归还。它也让事务内多个 helper 共享同一连接成为可能，例如 checksum 命中复用时对 `file_shared`、内部 `file_info` 和 `artifact_info` 的更新必须使用同一个 session。这里要注意 manager 的生命周期必须长于所有连接指针；项目通过单例和应用停止顺序保证这一点。

### 78. 制品元数据为什么拆成 `artifact_info`、`file_info` 和 `file_shared`？索引如何设计？

**参考回答：**

当前业务模型是 `artifact_info + file_shared + file_info`。`artifact_info` 是制品业务表，保存 project、version、build_no、artifact_name、checksum、file_id、branch、commit_id 等元数据；`file_shared` 是物理内容表，以唯一 `file_md5` 保存 `file_id`、大小和引用数；`file_info` 主要作为上传链路内部逻辑记录复用，不再对外暴露普通文件接口。热点查询包括按 project 查询制品列表、latest、versions、builds，以及按制品坐标定位下载和删除。压测时主要看这些 artifact 查询和下载定位，而不是旧的用户文件列表。

### 79. 哪些业务必须使用数据库事务？

**参考回答：**

checksum 命中并复用已有物理文件时，“物理引用数 +1”“内部 `file_info` 记录”和“`artifact_info` 制品元数据”必须一起成功或一起失败；直传或分片落库时 `file_info`、`file_shared` 与 `artifact_info` 必须同时创建；删除时删除 `artifact_info`、内部文件记录、引用数 -1、必要时删除 shared 记录必须在同一事务。代码在 `UploadServlet`、`DirUploadServlet`、`ChunkUploadServlet` 和 `DeleteFileServlet` 中使用 `soci::transaction`，失败显式 rollback，成功 commit。

### 80. 删除制品能做到 MySQL 和 FastDFS 强一致吗？

**参考回答：**

当前只能保证数据库内部一致。删除制品时，数据库事务会删除 `artifact_info` 和内部文件记录，并更新 `file_shared.ref_count`；事务提交后，如果引用数为 0，再调用 `FastDFSUtil::deleteFile()`。这个外部删除失败时，DB 已经没有 shared 记录，而且当前代码没有检查并补偿删除结果，因此可能残留 FastDFS 孤儿对象。之所以把物理删除放在事务后，是避免持有 DB 事务等待网络 IO。生产方案应使用 outbox/清理任务：事务内记录待删除 file_id，后台重试，成功后标记完成。

## 七、测试、压测与性能分析（81—90）

### 81. 项目是如何做压测的？

**参考回答：**

项目把环境固定在 Docker Compose 网络内，MySQL、FastDFS、Nginx 和 FiberServer 一起运行。`docker_bench.sh` 测纯 `/api/status`，`docker_bench_artifact.sh` 会自动创建 project token、预置一个测试制品，然后压测 `status`、`artifact_list`、`latest`、`versions`、`builds`、`download_header`，并用小样本压 `upload/direct`。指标记录成功数、错误数、HTTP 状态、实际 QPS、平均延迟、P50/P95/P99 和最大延迟。旧的 login/myfiles/download 业务压测脚本已经删除，只作为历史数据保留。

### 82. 当前可以对外讲的性能数据是什么？

**参考回答：**

可以稳定讲的旧基线是：纯 `/api/status` 在 100 并发、1000 请求下约 922 QPS。旧普通文件读接口在 SOCI 调整后曾经做到 100 并发、1000 请求全成功，读接口约 787~901 QPS，但这属于历史接口数据，不能再当作当前公开业务接口。当前新增的 `docker_bench_artifact.sh` 用来压 artifact 公开接口，短样本已验证读接口和小样本直传上传 0 错误；如果要写进简历，应重新跑 100/1000 或更长时间样本后再引用具体 QPS。

### 83. 为什么压测要同时看 P95/P99，不能只看 QPS？

**参考回答：**

QPS 只能说明吞吐，不能说明少量请求是否卡很久。项目早期旧接口 100 并发基线里 login 和 download 各有一次约 15 秒超时，平均值和大多数 P95 仍可能看起来正常。后来固定速率压测里 1200 RPS 档平均延迟约 12ms、P95 约 47ms，但仍有极少数 15 秒最大值。这些数据是历史性能排查背景，不等同于当前 artifact 业务结论；当前制品接口也必须看 P95/P99、最大延迟和错误数，才能区分稳定吞吐与“多数快、少数灾难性慢”。

### 84. 你如何判断 1250 RPS 已进入临界区？

**参考回答：**

不是只看是否跑完，而是比较相邻档位的延迟增长。历史固定速率测试中，1000 RPS 时平均约 7.08ms、P95 约 22.64ms；1200 时平均约 12.28ms、P95 约 46.62ms；1250 时平均约 17.27ms、P95 约 78.86ms、P99 接近 196ms。吞吐只增加几十 RPS，尾延迟却显著放大，说明排队效应开始出现。这个判断方法可以沿用到 artifact 接口，但具体稳定区间必须用 `docker_bench_artifact.sh` 重新测，不能直接拿旧业务数据替代。

### 85. 如何判断瓶颈是不是 MySQL？

**参考回答：**

我会把请求总耗时与 `db_ms`、SOCI 的 `pool_wait_ms/create_ms` 对齐，再看 MySQL `Slow_queries` 和关键查询的 `EXPLAIN`。当前 artifact 读接口主要查询 `artifact_info`，包括按 project 列表、latest、versions、builds 和按制品坐标下载定位；如果这些查询的 `db_ms` 很低、连接池没有等待、`/api/status` 没有调度队列积压，就不能简单把长尾归因于 MySQL。旧的 `/api/myfiles` 索引优化只能作为历史迁移经验，当前要按 artifact 查询重新验证执行计划。

### 86. 项目有哪些性能埋点？

**参考回答：**

Servlet 使用 `ScopedPerfLog` 记录 `total_ms`，并通过 `PerfTimer` 累加 `db_ms`、`fastdfs_ms`、`file_io_ms`；SOCI 池单独记录连接来源、`pool_wait_ms`、`create_ms`、池总连接数和 idle 数。纯吞吐对比时可以设置 `FIBER_PERF_LOG=0`，避免 stdout 日志 IO 干扰结果；定位慢请求时再开启分段日志。调度侧则通过 `/api/status` 看全局、本地和 steal 队列。

### 87. 如何利用调度统计判断 GMP 改造是否有效？

**参考回答：**

外部提交任务应增加 `global_schedule_count/global_execute_count`；fiber `YieldToReady()` 或事件恢复后，应看到 `schedule_count/local_execute_count` 增加；制造单 P 积压时应出现 `steal_count/steal_execute_count`。如果所有执行都来自 global，说明本地队列没有发挥作用；如果 steal 尝试远大于成功次数，需要增加退避；如果压测结束 `queue_size` 或 `global_queue_size` 长期不为零，说明服务已经过载或存在丢失唤醒/停止问题。

### 88. 单元测试覆盖了哪些关键风险？

**参考回答：**

当前 `test` 可执行文件覆盖 HMAC 已知向量、GMP 首轮全局与恢复后本地调度、批量 work stealing、sleep timer hook，以及 socket 的 connect/accept/read/write hook。它重点保护调度和 IO 唤醒闭环。缺口也很明显：缺少并发 close、IO 超时与就绪竞态、DB waiter 超时竞态、HTTP parser 边界、同 checksum 并发复用和 FastDFS 失败补偿等测试。

### 89. E2E 测试覆盖什么？

**参考回答：**

`scripts/docker_e2e.sh` 在真实 MySQL、FastDFS、Nginx 和应用实例上执行服务状态检查、旧公开接口 404、项目 token 创建、制品预检、直传上传、坐标 checksum 冲突、制品列表、download header、Nginx 完整下载、latest/versions/builds、删除，以及分片上传和分片下载。它还覆盖 body 分片和 Nginx `X-File-Path` 落盘分片两种模式。当前脚本没有直接查询 `file_shared.ref_count`，也没有在最后一个引用删除后直接验证 FastDFS 物理对象已消失；这两项应作为后续更严格的一致性测试补上。

### 90. 当前压测结论有哪些局限？

**参考回答：**

现有固定速率测试主要是 20 秒级，上传只用了少量小样本，不能代表长时间运行、大文件、慢客户端或磁盘打满场景；环境是单机 Docker，不能外推到多机 FastDFS 和真实网络；极少数 15 秒长尾尚未完全归因；日志开关、keep-alive 和 Nginx 是否运行也会显著影响数据。因此我会给出环境、命令和样本量，不把一次短测峰值包装成生产 SLA。

## 八、最大困难、真实 Bug 与压力追问（91—100）

### 91. 这个项目中你遇到的最大并发 Bug 是什么？

**参考回答：**

最典型的是 FastDFS C 客户端与 syscall hook 组合后的重复 fd waiter。`use_connection_pool=false` 时，并发协程可能拿到同一个全局 tracker `ConnectionInfo` 槽位；一个协程在 `connect/recv/send` 被 hook 挂起后，另一个协程复用同一 socket，又对相同 fd 和事件调用 `IOManager::addEvent()`，最终出现重复注册、上传 502，严重时进程断言退出。打开 FastDFS 客户端 `use_connection_pool=true` 后，并发请求获得独立的 pooled connection/socket，问题未再复现。

### 92. 你是怎么定位 FastDFS 重复 fd Bug 的？

**参考回答：**

我先把现象从业务错误缩小到运行时：40 请求/20 并发时大量上传 502，200/50 时出现 duplicate fd event 并崩溃。然后沿 `uploadSmallFile → FastDFS C client → socket hook → IOManager::addEvent` 加连接和 fd 观测，发现多个协程共用了同一底层连接。最后对 `use_connection_pool=false/true` 做 A/B：关闭时稳定复现，开启后 40/20 全成功，200/50 关闭 keep-alive 时 200/200 成功，且不再出现 hook 断言。由此确认根因不是 FastDFS 吞吐，而是连接对象并发复用破坏了“一 fd 同类事件只有一个 waiter”的假设。

### 93. 为什么不简单给 FastDFS 调用加一把全局锁？

**参考回答：**

全局锁确实能规避同一连接并发使用，但会把所有上传串行化，协程和连接池的并发收益基本消失。更合理的是让并发请求使用独立 `ConnectionInfo/socket`，所以启用 FastDFS 客户端连接池，并由 `FastDFSManager` 管理包装对象。如果第三方库无法保证连接隔离，另一个可接受方案是把 FastDFS 调用放到专用阻塞 worker，并按连接粒度串行，而不是锁住整个存储服务。

### 94. 项目里出现过 15 秒长尾，你怎么分析和表述？

**参考回答：**

早期 100 并发基线中旧 login、download 各出现一个约 15 秒超时。补齐 SOCI 连接池、关键事务和 DB worker 后，同样 100 并发重跑旧读接口未复现。但这些是旧普通文件接口的数据，当前公开业务已经变成 `/api/artifacts/*`。现在更严谨的说法是：历史长尾帮助我补齐了 DB worker、连接池和性能埋点；当前 artifact 接口需要用 `docker_bench_artifact.sh` 重新跑足够样本后再判断是否还有同类长尾，不能沿用旧接口结论。

### 95. 为什么 DB worker 和协程化连接池要同时存在？

**参考回答：**

两者解决不同阻塞层次。`DbExecutor` 把整个同步 SQL 调用从 HTTP worker 隔离出去，即使 SOCI/MySQL 内部有无法 hook 的阻塞，也不会直接卡住 HTTP 线程。`SociManager` 的协程 waiter 则解决 DB worker 在“连接池已满”时的等待：不使用 condition_variable 阻塞真实 DB 线程，而是把 fiber 挂起，连接归还或超时后恢复。只做前者，DB worker 可能全堵在拿连接；只做后者，实际 SQL 仍可能占住 HTTP worker。

### 96. GMP 改造后停止逻辑出现了什么新风险？

**参考回答：**

原调度器只有全局 `m_fibers`，停止时检查全局队列即可。增加 P 本地队列后，任务可能已经从全局批量搬走，但还没执行；如果沿用旧条件，`stop()` 会误以为无任务并让 idle fiber 退出。当前 `Scheduler::stopping()` 在检查 `m_autoStop/m_stopping/active/global` 后，还调用 `processorQueuesEmpty()` 逐个确认本地队列为空；`IOManager` 还叠加 timer 和 pending event 条件。

### 97. 协程“挂起前被唤醒”为什么危险？项目如何处理？

**参考回答：**

典型时序是 fiber 注册 fd 事件后，另一个 epoll 线程立刻发现就绪并重新 schedule，但原 fiber 还没执行完 `swapOut()`。如果它已被标成可运行，另一个线程可能同时 resume 同一栈，造成上下文破坏。当前做法是 `YieldToHold()` 切出前保持 `EXEC`，切回 Scheduler 后才改 `HOLD`，取队列时跳过 `EXEC` 项。这个区域仍需要重点压测，因为本地队列若直接丢弃过早入队的 `EXEC` 项可能形成 lost wakeup；更完整的方案应实现原子的 park/unpark 握手，而不是只靠状态检查。

### 98. 从 ucontext 迁移到 Boost.Context 的难点是什么？

**参考回答：**

难点不是替换 API 名称，而是调用方上下文和对象生命周期。旧实现手动分配栈并 `swapcontext`；新实现用 `fixedsize_stack`，`m_ctx/m_caller` 都是 move-only fiber，每次 `resume()` 后必须接住返回对象。普通任务通过 `MainFunc()` 最终 `swapOut()`，`use_caller` 根 fiber 通过 `CallerMainFunc()/back()` 回到创建线程。迁移后要重新验证 reset、异常结束、主 fiber 析构、sleep 和 socket hook，因为这些路径都依赖切换后状态正确。

### 99. 制品引用计数最容易出什么一致性 Bug？

**参考回答：**

如果“新增 `artifact_info` 制品记录”和 `file_shared.ref_count+1` 不在同一事务，失败会造成引用虚高，或者业务上能查到制品但物理引用没有增加；删除时若先删 FastDFS 物理文件再提交 DB，事务失败会让其他制品坐标指向不存在内容。当前把 `artifact_info`、内部 `file_info` 和 `file_shared` 的写操作放在同一个 SOCI 事务，并在引用归零、事务提交后才删 FastDFS。剩余问题是物理删除失败没有可靠补偿，以及同一制品坐标并发上传时需要依赖数据库唯一键和幂等冲突处理兜底，这两点是生产化优先项。

### 100. 如果让你继续迭代一次，你会优先改什么？

**参考回答：**

我会先做正确性而不是继续追峰值：第一，给 fiber 挂起/唤醒增加原子状态机并补并发竞态测试；第二，为上传和物理删除增加任务表、幂等键和补偿重试，补服务端 checksum/大小校验；第三，强化制品坐标唯一键和重复上传幂等处理；第四，完善项目 token 管理、读接口鉴权、审计日志和 RBAC；第五，接入 Prometheus，记录请求、fd、timer、DB waiter、FastDFS 池和连接级长尾；最后再用 `docker_bench_artifact.sh` 跑分钟级、小时级和大文件压测，根据数据调整 HTTP/DB worker、连接池及本地队列策略。

## 面试回答提醒

- 不要说“完整实现 Go GMP”，要说“借鉴 GMP 的简化 M:N 调度模型”。
- 不要说“稳定 1300 QPS”，要区分峰值、临界值和稳定区间。
- 被问到 Bug 时按“现象—缩小范围—根因—修复—验证—剩余风险”回答。
- 被问到性能时必须同时给出环境、请求类型、并发、样本量、P95/P99 和错误数。
- 遇到当前代码确实没解决的问题，直接说明限制和改进方案，不要虚构已经实现。
