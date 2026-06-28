# FiberServer 压测结果记录

本文档记录当前 Docker/本机环境下的压测结果。数据用于判断本次简化版 GMP 改造后的服务稳定档位和性能边界。

## 测试环境

- 服务入口：`fiberserver-app:8080`
- 下载入口：`nginx`
- 运行方式：Docker Compose 网络内运行压测脚本
- 压测脚本：
  - `scripts/docker_bench.sh`
  - `scripts/docker_bench_artifact.sh`
- 业务依赖：
  - MySQL
  - FastDFS tracker
  - FastDFS storage
  - Nginx

## 纯状态接口压测

命令形态：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e REQUESTS=1000 \
  -e CONCURRENCY=100 \
  fiberserver-dev bash scripts/docker_bench.sh
```

结果：

| 接口 | 请求数 | 并发 | 成功 | 错误 | QPS | 平均延迟 | P95 | P99 | 最大延迟 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `/api/status` | 1000 | 100 | 1000 | 0 | 922.03 | 60.03ms | 112.72ms | 146.07ms | 208.22ms |

结论：纯状态接口在 100 并发下可以稳定完成，吞吐约 900 QPS。

## 制品仓库业务压测

命令形态：

```bash
docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e BASE_URL=http://fiberserver-app:8080 \
  -e DOWNLOAD_HEADER_BASE_URL=http://fiberserver-app:8080 \
  -e REQUESTS=1000 \
  -e CONCURRENCY=100 \
  -e UPLOAD_REQUESTS=10 \
  -e UPLOAD_CONCURRENCY=3 \
  fiberserver-dev bash scripts/docker_bench_artifact.sh
```

脚本会自动创建项目 token，预置一个测试制品，然后压测当前公开业务接口：

- `GET /api/status`
- `POST /api/artifacts/list`
- `GET /api/artifacts/latest`
- `GET /api/artifacts/versions`
- `GET /api/artifacts/builds`
- `HEAD /api/artifacts/download`
- `POST /api/artifacts/upload/direct` 小样本直传上传

参数说明：

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| `REQUESTS` | `200` | 每个读接口请求数 |
| `CONCURRENCY` | `20` | 每个读接口并发数 |
| `UPLOAD_REQUESTS` | `5` | 直传上传请求数，设为 `0` 可关闭写压测 |
| `UPLOAD_CONCURRENCY` | `2` | 直传上传并发数 |
| `BENCH_KEEPALIVE` | `1` | 是否复用 HTTP 连接 |

制品仓库压测更贴近当前业务层，因为它不再依赖旧的登录、普通文件列表和普通下载接口。默认上传样本较小，主要用于确认写链路延迟和稳定性，不代表大文件上传极限。

## 历史业务接口压测

以下数据来自旧普通文件接口仍存在时的历史压测，用于说明早期性能边界。当前公开业务接口已经收敛到 `/api/artifacts/*`，对应的旧压测脚本已删除。

旧业务压测脚本曾自动准备测试用户和样例文件，覆盖：

- `status`
- `login`
- `myfiles`
- `download`
- `direct_upload`

有效结果如下：

| 场景 | 请求/并发 | 结果 |
| --- | ---: | --- |
| 业务链路 | 100 / 10 | 全成功，读接口约 734-821 QPS |
| 业务链路 | 200 / 20 | 全成功，读接口约 677-850 QPS |
| 业务链路 | 300 / 30 | 全成功，读接口约 787-872 QPS |
| 业务链路 | 500 / 50 | 全成功，读接口约 640-831 QPS |
| 业务链路 | 800 / 80 | 全成功，读接口约 743-854 QPS |
| 业务链路 | 1000 / 100 | SOCI 迁移后重跑全成功，未复现 15 秒级超时 |

80 并发详细结果：

| 接口 | 请求数 | 并发 | 成功 | 错误 | QPS | 平均延迟 | P95 | P99 | 最大延迟 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| status | 800 | 80 | 800 | 0 | 801.59 | 52.90ms | 92.31ms | 108.47ms | 128.68ms |
| login | 800 | 80 | 800 | 0 | 743.47 | 66.33ms | 124.03ms | 151.84ms | 190.62ms |
| myfiles | 800 | 80 | 800 | 0 | 769.27 | 68.03ms | 120.56ms | 150.10ms | 240.18ms |
| download | 800 | 80 | 800 | 0 | 853.61 | 47.85ms | 81.48ms | 100.30ms | 123.30ms |
| direct_upload | 10 | 3 | 10 | 0 | 104.34 | 23.37ms | 28.40ms | 28.40ms | 28.40ms |

100 并发详细结果（早期基线，SOCI 连接池和事务补齐前）：

| 接口 | 请求数 | 并发 | 成功 | 错误 | QPS | 平均延迟 | P95 | P99 | 最大延迟 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| status | 1000 | 100 | 1000 | 0 | 825.35 | 59.82ms | 108.19ms | 138.54ms | 181.91ms |
| login | 1000 | 100 | 999 | 1 | 62.64 | 98.16ms | 162.19ms | 210.13ms | 15112.26ms |
| myfiles | 1000 | 100 | 1000 | 0 | 754.11 | 71.43ms | 121.19ms | 149.18ms | 194.33ms |
| download | 1000 | 100 | 999 | 1 | 62.67 | 72.90ms | 110.70ms | 134.23ms | 15057.43ms |
| direct_upload | 10 | 3 | 10 | 0 | 91.45 | 28.57ms | 36.28ms | 36.28ms | 36.28ms |

100 并发详细结果（SOCI 迁移后重跑）：

| 接口 | 请求数 | 并发 | 成功 | 错误 | QPS | P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| status | 1000 | 100 | 1000 | 0 | 857.55 | 109.19ms |
| login | 1000 | 100 | 1000 | 0 | 787.59 | 113.82ms |
| myfiles | 1000 | 100 | 1000 | 0 | 808.99 | 122.88ms |
| download | 1000 | 100 | 1000 | 0 | 901.75 | 98.49ms |
| direct_upload | 10 | 3 | 10 | 0 | 112.41 | 46.45ms |

HTTP keep-alive 对比（Nginx 入口，300 请求 / 30 并发，`FIBER_PERF_LOG=0`）：

| 接口 | 短连接 QPS | keep-alive QPS | 短连接 P95 | keep-alive P95 |
| --- | ---: | ---: | ---: | ---: |
| status | 811.20 | 1145.58 | 44.34ms | 35.01ms |
| login | 700.68 | 892.00 | 55.20ms | 39.11ms |
| myfiles | 667.10 | 826.89 | 57.31ms | 50.36ms |
| download | 759.69 | 1078.16 | 53.84ms | 30.19ms |

## 数据库与 SQL 优化后压测

本轮变更：

- `file_info.url` 已改名为 `file_info.owner`，避免把文件 owner 误称为 URL。
- `file_info` 增加组合索引：`idx_owner_filename(owner, filename)`、`idx_owner_id(owner, id)`、`idx_owner_md5(owner, md5)`。
- `/api/myfiles` 增加分页参数，默认 `limit=100`，最大 `1000`。
- 热点单行查询增加 `LIMIT 1`，用户文件列表固定走 `idx_owner_id`，避免 `filesort`。
- 数据库连接池等待已协程化，短请求不会因等待连接阻塞真实 HTTP worker。
- 同步 SQL 已下沉到 DB worker，覆盖 `login/register/myfiles/download/upload/md5/deletefile/dirupload/chunkupload` 等主要路径。
- `file_shared` 作为物理文件元数据和引用计数表使用，秒传存在性、物理文件大小、引用计数走 `file_shared`；`file_info` 保持用户文件记录和热路径列表查询。

验证：

- Release 构建通过。
- `./build/test` 通过。
- `scripts/docker_e2e.sh` 通过。
- `EXPLAIN` 确认 `/api/myfiles` 列表查询走 `idx_owner_id`，无 `filesort`。
- MySQL 压测后 `Slow_queries=0`，未观察到查询堆积。

压测配置：

该混合 RPS 压测依赖旧普通文件接口，当前脚本已删除；以下结果仅作为历史记录保留。

结果：

| 目标 RPS | 时长 | Workers | 成功 | 错误 | 实际 RPS | 平均延迟 | P50 | P95 | P99 | 最大延迟 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1000 | 20s | 180 | 19999 | 1 | 996.95 | 7.08ms | 2.05ms | 22.64ms | 113.28ms | 15015.42ms |
| 1200 | 20s | 180 | 23998 | 2 | 1195.47 | 12.28ms | 4.81ms | 46.62ms | 87.79ms | 15084.09ms |
| 1250 | 20s | 180 | 24999 | 1 | 1239.93 | 17.27ms | 4.67ms | 78.86ms | 196.32ms | 15023.82ms |

分接口结果（1200 RPS 档）：

| 接口 | 请求数 | 错误 | 平均延迟 | P95 | P99 | 最大延迟 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| status | 6000 | 0 | 8.03ms | 36.09ms | 71.09ms | 164.42ms |
| login | 5999 | 0 | 13.28ms | 56.08ms | 101.13ms | 264.01ms |
| myfiles | 5999 | 0 | 13.53ms | 56.45ms | 99.78ms | 224.60ms |
| download | 6000 | 0 | 9.29ms | 36.68ms | 72.26ms | 144.13ms |

结论：

数据库侧当前没有慢查询或连接池等待堆积，SQL 执行计划也命中新索引。压测中仍偶发 15 秒级超时，但服务端日志、MySQL `Slow_queries` 和 `/api/status` 调度队列均未显示数据库瓶颈；该长尾更可能来自 keep-alive/TCP 连接层或压测客户端连接复用路径。当前环境下较稳的业务混合吞吐档位约在 `1000-1200 RPS`，`1250 RPS` 进入临界区，`1300 RPS` 仍不稳定。

## 结论

当前环境下可以认为：

```text
SOCI 迁移后，业务读接口在 100 并发、1000 请求样本下全成功。
读接口吞吐约 787-901 QPS，未复现早期 login/download 的 15 秒级超时。
HTTP keep-alive 对短接口有明确收益，300/30 样本下读接口 QPS 约提升 24%-41%。
```

压测后健康检查：

- `/api/status` 返回 `HTTP/1.1 200 OK`
- `global_queue_size = 0`
- 调度器没有观察到队列积压

## 注意事项

- 早期有一次 30 并发压测出现大量下载失败，后来确认当时 Nginx 未运行，该轮数据不作为有效结果。
- 早期 100 并发基线出现过 `login/download` 各 1 个 15 秒级超时；SOCI 连接池和关键事务补齐后重跑未复现。
- 当前上传压测只使用小样本，主要用于确认链路可用，不代表上传极限性能。
- 压测脚本默认启用 `BENCH_KEEPALIVE=1`；如需复现短连接基线，可设置 `BENCH_KEEPALIVE=0`。
- 当前服务已补性能分段日志：`total_ms`、`db_ms`、`fastdfs_ms`、`file_io_ms`，以及 SOCI 连接池的 `pool_wait_ms/create_ms`。排查瓶颈时建议开启；做纯吞吐对比时建议用 `FIBER_PERF_LOG=0` 关闭，避免日志 IO 影响结果。
- 如果后续继续优化，建议补更长时间压测和更大的上传样本，而不是只依赖当前短样本结果。
