# FiberServer 文档索引

本目录用于放置项目当前可交付、可运行、可复盘的说明材料。

## 推荐阅读顺序

1. [PROJECT_PRESENTATION.md](PROJECT_PRESENTATION.md)
   - 简历、答辩和面试介绍用的主表述。
   - 重点说明为什么项目应定位为 CI/CD 构建制品仓库，而不是普通云存储。
2. [ARTIFACT_REPOSITORY.md](ARTIFACT_REPOSITORY.md)
   - 说明制品仓库数据模型、token 鉴权、坐标不可变和 `/api/artifacts/*` 接口。
3. [GMP_IMPLEMENTATION_SUMMARY.md](GMP_IMPLEMENTATION_SUMMARY.md)
   - 说明简化版 GMP 调度器的目标、核心实现、测试覆盖和边界。
4. [WSL2_DOCKER.md](WSL2_DOCKER.md)
   - 说明如何在 WSL2/Docker Compose 环境中构建、测试、运行服务。
5. [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md)
   - 记录当前环境中的压测数据、稳定并发档位和已知边界。

## 文档分工

| 文档 | 用途 |
| --- | --- |
| `docs/PROJECT_PRESENTATION.md` | 项目定位、简历写法、答辩讲法、表述边界 |
| `docs/ARTIFACT_REPOSITORY.md` | 构建制品仓库业务语义、接口和数据模型 |
| `docs/GMP_IMPLEMENTATION_SUMMARY.md` | GMP 风格协程调度器实现总结 |
| `docs/WSL2_DOCKER.md` | 开发、构建、运行、验证指南 |
| `docs/FASTDFS_HOOK.md` | FastDFS 客户端与 syscall hook 并发问题验证 |
| `docs/BENCHMARK_RESULTS.md` | 当前压测数据和结论 |
| `docs/SOCI_MIGRATION.md` | SOCI 数据库访问层迁移记录 |

## 当前项目状态

- 主业务定位已调整为 CI/CD 构建制品仓库。
- 已实现独立 `artifact_info` 元数据表和 `/api/artifacts/*` 接口。
- Artifact 写接口已接入项目 token 鉴权。
- 制品坐标已做不可变保护：同坐标不同 checksum 会返回冲突。
- 简化版 GMP 调度器已实现：`Processor`、本地队列、全局队列批量搬运、任务窃取。
- `/api/status` 可输出调度器和每个 Processor 的运行统计。
- Docker 开发环境、端到端脚本和压测脚本已补齐。

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
