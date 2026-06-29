# 构建制品仓库模式

本项目支持 CI/CD 构建制品仓库语义。底层仍复用 C++ M:N 协程调度器、epoll/hook、HTTP servlet、FastDFS、MySQL 和 Nginx 下载转发；业务元数据已经从旧文件列表中拆出，单独落到 `artifact_info` 表。

## 定位

构建制品仓库面向 CI/CD 流水线和部署系统，用于存放编译、打包、测试后生成的构建产物，例如：

- `service-1.2.0.jar`
- `web-dist.zip`
- `server-linux-x64.tar.gz`
- `app-release.apk`
- 测试报告、覆盖率报告、部署包

相比普通云存储，它更强调版本、构建号、checksum 校验、去重上传和多节点高并发下载分发。

## 数据模型

`file_shared` 仍作为物理文件去重表，保存 checksum 到 FastDFS `file_id` 的映射和引用计数。

`file_info` 现在只作为制品上传链路的内部文件记录表复用，不再暴露旧的普通文件公开接口。旧的 `/api/upload`、`/api/myfiles`、`/api/download`、`/api/deletefile` 等路由已经移除。

`artifact_info` 保存制品仓库业务元数据：

```sql
artifact_info(
  id,
  project_name,
  version,
  build_no,
  artifact_name,
  checksum,
  file_id,
  size,
  artifact_type,
  branch,
  commit_id,
  create_time,
  update_time
)
```

其中 `(project_name, version, build_no, artifact_name)` 是制品唯一键。这样同一个项目可以保留不同版本、不同构建号下的同名制品。

## 字段兼容

Artifact API 使用新的业务字段：

| 制品字段 | 说明 |
| --- | --- |
| `project_name` / `project` / `namespace` | 项目或命名空间 |
| `checksum` | 制品校验值和去重 key |
| `artifact_name` | 制品名 |
| `artifact_type` | 制品 MIME/type |
| `version` | 版本号 |
| `build_no` | 构建号 |
| `branch` | 构建来源分支 |
| `commit_id` | 构建来源提交 |

Artifact API 只建议使用制品字段。实现内部仍会把部分字段映射到历史文件记录结构中，但公开路由已经收敛到 `/api/artifacts/*`，不再支持旧客户端直接访问普通文件接口。

## Artifact API

| 接口 | 用途 |
| --- | --- |
| `POST /api/artifacts/precheck` | 上传前检查，支持 checksum 秒传判断 |
| `POST /api/artifacts/upload/direct` | 小制品直传 |
| `POST /api/artifacts/upload/chunk` | 大制品分片上传 |
| `POST /api/artifacts/list` | 查询项目制品列表 |
| `POST /api/artifacts/checksum` | checksum 存在性检查 |
| `GET /api/artifacts/download` | 下载制品 |
| `POST /api/artifacts/delete` | 删除制品记录 |
| `POST /api/artifacts/token` | 创建或轮换项目上传 token |
| `GET /api/artifacts/latest` | 查询项目最新制品 |
| `GET /api/artifacts/versions` | 查询项目版本列表 |
| `GET /api/artifacts/builds` | 查询项目指定版本下的构建号列表 |

## Token 鉴权

Artifact 写接口需要项目 token：

- `POST /api/artifacts/precheck`
- `POST /api/artifacts/upload/direct`
- `POST /api/artifacts/upload/chunk`
- `POST /api/artifacts/delete`

客户端使用 `Authorization: Bearer <token>` 或 `X-Artifact-Token` 传入 token。
服务端只在 `project_token` 表保存 token 哈希，不保存明文。`POST /api/artifacts/token`
是为了演示和 CI e2e 闭环提供的轻量创建/轮换接口；生产环境建议把它放到管理员控制面，
或者改成运维侧预置密钥。

创建或轮换 token：

```bash
curl -X POST http://localhost:8080/api/artifacts/token \
  -H 'Content-Type: application/json' \
  -d '{"project_name":"auth-service","token":"ci-secret"}'
```

## 坐标不可变

`project_name + version + build_no + artifact_name` 被视为一个制品坐标。
同一坐标重复上传相同 checksum 会被当成已存在制品复用；如果 checksum 不同，
服务端会返回 `artifact checksum conflict`，避免同一版本/构建号下的制品被静默覆盖。

## 示例

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

直传：

```bash
curl -X POST \
  'http://localhost:8080/api/artifacts/upload/direct?project_name=auth-service&version=1.2.0&build_no=104&branch=main&commit_id=8f31c9a&artifact_name=auth-service.tar.gz&checksum=abc123&size=4096&artifact_type=application/gzip' \
  -H 'Authorization: Bearer ci-secret' \
  --data-binary @auth-service.tar.gz
```

列表：

```bash
curl -X POST http://localhost:8080/api/artifacts/list \
  -H 'Content-Type: application/json' \
  -d '{"project_name":"auth-service"}'
```

下载：

```bash
curl -I \
  'http://localhost:8080/api/artifacts/download?project_name=auth-service&version=1.2.0&build_no=104&artifact_name=auth-service.tar.gz'
```

查询最新制品：

```bash
curl 'http://localhost:8080/api/artifacts/latest?project_name=auth-service'
```

查询版本列表：

```bash
curl 'http://localhost:8080/api/artifacts/versions?project_name=auth-service'
```

查询构建号列表：

```bash
curl 'http://localhost:8080/api/artifacts/builds?project_name=auth-service&version=1.2.0'
```

## 面试表述

推荐表述：

> 基于 C++ 协程调度与异步 IO 的高并发构建制品仓库，面向 CI/CD 流水线提供构建产物上传、版本化元数据管理、checksum 去重、分片上传和高并发下载分发能力。底层借鉴 Go GMP 调度思想，实现 C++ M:N 协程调度器，并结合 epoll、hook、FastDFS 和 MySQL 支撑 IO 密集型场景。

避免表述为“完整复刻 Go runtime”或“完整实现 Artifactory/Nexus”。当前实现是面向项目展示和高并发 IO 验证的轻量制品仓库，不包含完整包管理协议和企业级权限体系。
