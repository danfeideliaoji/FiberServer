# FiberServer 文档索引

本文档目录用于放置项目当前可交付、可运行、可复盘的说明材料。

## 推荐阅读顺序

1. [GMP 改造说明](GMP_IMPLEMENTATION_SUMMARY.md)
   - 说明原项目基础、简化版 GMP 的目标、核心实现、测试覆盖和后续优化方向。
   - 如果要答辩、汇报或复盘本次改造，优先读这一份。

2. [WSL2 + Docker 开发环境](WSL2_DOCKER.md)
   - 说明如何在 WSL2/Docker Compose 环境中构建、测试、运行服务。
   - 包含 MySQL、FastDFS、Nginx、FiberServer 的启动方式和常用脚本。

3. [压测结果记录](BENCHMARK_RESULTS.md)
   - 记录当前环境下 `/api/status` 和业务接口的压测数据。
   - 包含稳定并发档位、QPS、延迟和已知边界。

## 文档分工

| 文档 | 用途 |
| --- | --- |
| `docs/GMP_IMPLEMENTATION_SUMMARY.md` | 本次 GMP 改造的最终说明 |
| `docs/WSL2_DOCKER.md` | 开发、构建、运行、验证指南 |
| `docs/BENCHMARK_RESULTS.md` | 当前压测数据和结论 |
## 当前项目状态

- 简化版 GMP 调度器已实现：`Processor`、本地队列、全局队列批量搬运、任务窃取。
- `/api/status` 已能输出调度器和每个 P 的运行统计。
- Docker 开发环境、端到端脚本和压测脚本已补齐。
- 数据库访问使用 SOCI 后端，旧 MySQL C API 路径已移除。
- SOCI 迁移后，当前环境下业务读接口在 100 并发、1000 请求样本下全成功，吞吐约 787-901 QPS。
- 后续优化应优先补更长时间压测、扩大上传样本，并按生产连接数和延迟继续调参。

## 常用命令

构建镜像：

```bash
bash scripts/docker_build.sh
```

编译并运行测试：

```bash
bash scripts/docker_test.sh
```

启动服务：

```bash
bash scripts/docker_run_server.sh
```

端到端验证：

```bash
bash scripts/docker_e2e.sh
```

业务压测：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e DOWNLOAD_BASE_URL=http://nginx \
  -e REQUESTS=800 \
  -e CONCURRENCY=80 \
  -e UPLOAD_REQUESTS=10 \
  -e UPLOAD_CONCURRENCY=3 \
  fiberserver-dev bash scripts/docker_bench_business.sh
```
