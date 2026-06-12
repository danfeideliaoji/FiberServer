# MySQL C API to SOCI Migration Plan

本文档说明如何把当前项目里的 MySQL 底层 C API 封装逐步迁移到 SOCI。

目标不是一次性重写数据库层，而是先把业务代码从 `MYSQL_BIND`、`mysql_stmt_*`、结果缓冲区和列下标解析里解耦出来，让上传、下载、登录、文件列表这些路径更容易维护。

参考资料：

- SOCI 官方文档：https://soci.sourceforge.net/doc/master/
- MySQL Connector/C++ 官方文档：https://dev.mysql.com/doc/dev/connector-cpp/latest/

## 当前问题

当前数据库实现主要在：

- `FiberServer/db/mysql.h`
- `FiberServer/db/mysql.cpp`
- `FiberServer/my/mysqlop.h`
- `FiberServer/my/mysqlop.cpp`

已有代码封装了连接、预处理语句、结果集、事务和连接池，但业务维护成本仍然偏高：

- 需要手动维护 `MYSQL_BIND`、buffer 长度、字段类型和释放逻辑。
- `FileInfoFromResult` 依赖 SELECT 字段顺序，字段顺序不一致时容易读错。
- 少量查询仍然使用格式化 SQL，例如 `SELECT count FROM file_info WHERE file_id = '%s'`。
- 每次业务调用都会重新 prepare statement，当前封装没有语句缓存。
- 连接池配置使用 `min_conn/max_conn`，但配置文件里写的是 `connection`，配置语义不一致。

这些问题不一定都要靠 SOCI 解决，但 SOCI 能明显减少底层 API 代码量，让问题集中在业务 SQL、事务和 schema 设计上。

## 推荐方向

推荐使用 SOCI 替换当前直接基于 MySQL C API 的业务访问层。

不建议直接把所有 `MySQL`、`MySQLStmt`、`MySQLRes`、`MySQLManager` 一次性删掉。更稳的方式是：

1. 新增一层 SOCI 实现。
2. 保持 `user_info::GetUserByUsername`、`file_info::GetFileListByUser` 这类业务函数签名基本不变。
3. servlet 层先不感知 SOCI。
4. 逐个接口迁移并压测。
5. 等业务路径稳定后，再删除旧的 MySQL C API 封装。

## 非目标

第一阶段不做这些事：

- 不重构 HTTP servlet。
- 不重写调度器、hook、IOManager。
- 不改变接口 JSON 格式。
- 不引入 ORM。
- 不同时修改 FastDFS 文件存储逻辑。
- 不追求异步 MySQL 客户端。SOCI 是同步数据库访问库，是否能被当前 hook 机制协程化，需要单独验证。

## 依赖接入

如果继续使用 vcpkg，优先尝试：

```bash
vcpkg install soci[mysql]
```

CMake 侧建议先用独立开关引入，便于回退：

```cmake
option(USE_SOCI_DB "Use SOCI database backend" off)

if(USE_SOCI_DB)
    find_package(SOCI CONFIG REQUIRED)
endif()
```

链接目标名称需要以当前包管理器实际导出的 target 为准。常见形式可能是：

```cmake
target_link_libraries(fiberserver
    PUBLIC
        SOCI::soci_core
        SOCI::soci_mysql
)
```

如果本地 CMake 找不到这些 target，应先检查 vcpkg 安装输出和 `share/soci` 下的 config 文件，不要在业务代码里硬编码库路径。

## 建议目录结构

新增文件建议放在 `FiberServer/db/` 下：

```text
FiberServer/db/soci_db.h
FiberServer/db/soci_db.cpp
FiberServer/db/soci_pool.h
FiberServer/db/soci_pool.cpp
```

业务 CRUD 可以先继续放在：

```text
FiberServer/my/mysqlop.h
FiberServer/my/mysqlop.cpp
```

等 SOCI 路径稳定后，再考虑把 `mysqlop` 改名为更中性的 `dbop` 或 `storage_db`。第一阶段不建议改名，避免 servlet 层大面积改动。

## 目标接口形状

建议新封装只暴露非常小的接口：

```cpp
class SociSession {
public:
    soci::session& raw();
};

class SociPool {
public:
    std::shared_ptr<SociSession> get(int64_t timeout_ms = 3000);
};

class SociTransaction {
public:
    explicit SociTransaction(soci::session& sql);
    void commit();
};
```

业务层不要直接持有 `MYSQL*` 或 `MYSQL_STMT*`，也不要自己处理结果缓冲区。

## 查询示例

当前登录查询大致是：

```cpp
auto stmt = MySQLStmt::Create(db,
    "SELECT id, username, password, salt, nickname, status, last_login, create_time, update_time "
    "FROM user_info WHERE username = ?");
stmt->bindString(1, username);
auto res = stmt->query();
```

迁移后建议变成：

```cpp
soci::row row;
sql << "SELECT id, username, password, salt, nickname, status, last_login, create_time, update_time "
       "FROM user_info WHERE username = :username",
       soci::use(username),
       soci::into(row);

if (!sql.got_data()) {
    return nullptr;
}
```

更推荐再包一层 mapper，避免业务里散落字段读取：

```cpp
static std::shared_ptr<UserInfo> UserInfoFromRow(const soci::row& row) {
    auto info = std::make_shared<UserInfo>();
    info->id = row.get<long long>("id");
    info->username = row.get<std::string>("username");
    info->password = row.get<std::string>("password");
    info->salt = row.get<std::string>("salt");
    info->nickname = row.get<std::string>("nickname");
    info->status = static_cast<int8_t>(row.get<int>("status"));
    return info;
}
```

这样字段读取依赖列名，不再依赖 SELECT 字段下标。

## 事务示例

上传秒传路径会同时改 `file_shared` 和 `file_info`，建议迁移时优先纳入事务。

```cpp
soci::transaction tr(sql);

sql << "UPDATE file_shared SET ref_count = ref_count + 1 WHERE file_md5 = :md5",
       soci::use(md5);

sql << "INSERT INTO file_info (md5, file_id, url, filename, size, type, count) "
       "VALUES (:md5, :file_id, :username, :filename, :size, :type, 1)",
       soci::use(md5),
       soci::use(file_id),
       soci::use(username),
       soci::use(filename),
       soci::use(size),
       soci::use(type);

tr.commit();
```

如果后续要处理高并发秒传，建议配合唯一约束和 `INSERT ... ON DUPLICATE KEY UPDATE`，减少 `exists -> update/insert` 的竞态窗口。

## 分阶段迁移

### 阶段 1：引入 SOCI 但不替换业务

改动：

- CMake 增加 `USE_SOCI_DB` 开关。
- Docker/vcpkg 环境增加 SOCI MySQL backend 依赖。
- 新增一个最小 `soci_db` smoke test，验证能连上 MySQL 并执行 `SELECT 1`。

验证：

```bash
cmake -S . -B build -DUSE_SOCI_DB=ON
cmake --build build -j
```

### 阶段 2：迁移用户登录和注册

优先迁移：

- `user_info::GetUserByUsername`
- `user_info::CreateUser`
- `user_info::UpdateLastLogin`

原因：

- 查询简单。
- 影响路径清晰。
- 容易通过 register/login 接口验证。

验证：

- 注册新用户。
- 重复注册同名用户应失败。
- 正确密码登录成功。
- 错误密码登录失败。

### 阶段 3：迁移文件列表和文件定位

迁移：

- `file_info::GetFileListByUser`
- `file_info::GetFileByUserAndFilename`
- `file_info::DeleteFileRecordByUserAndFilename`

同时修正当前字段顺序问题，所有 `FileInfo` 查询都统一字段：

```sql
SELECT id, md5, file_id, url, filename, size, type, count, create_time, update_time
FROM file_info
...
```

验证：

- 上传后 `/api/myfiles` 能看到正确的 `filename/size/type`。
- 下载时能按用户名和文件名定位正确文件。
- 删除后文件列表不再返回该记录。

### 阶段 4：迁移秒传和引用计数

迁移：

- `file_shared::ExistsByMd5`
- `file_shared::GetFileIdByMd5`
- `file_shared::CreateShared`
- `file_shared::IncrementRef`
- `file_shared::DecrementRef`
- `file_shared::DeleteShared`

这一阶段要重点检查事务一致性。推荐把秒传路径中的 `file_shared` 更新和 `file_info` 插入放在同一事务里。

验证：

- 同一 MD5 多用户秒传后，`ref_count` 正确增加。
- 删除一个用户文件后，`ref_count` 正确减少。
- `ref_count = 0` 时才删除 FastDFS 文件和 `file_shared` 记录。

### 阶段 5：删除旧 MySQL C API 业务依赖

当所有业务 CRUD 都走 SOCI 后，再处理：

- 删除 `FiberServer/db/mysql.cpp` 中不再使用的 statement/result 封装。
- 保留或替换 `MySQLManager` 的配置加载能力。
- 清理 `mysqlclient` 链接项。

这个阶段必须在完整业务测试和压测通过后做。

## Schema 配套建议

迁移 SOCI 时可以顺手补索引，但建议独立提交，避免数据库访问库迁移和 schema 性能优化混在一起。

优先考虑：

```sql
ALTER TABLE file_info ADD INDEX idx_user_id (url, id);
ALTER TABLE file_info ADD INDEX idx_md5_user (md5, url);
```

如果业务不允许同一用户同名文件：

```sql
ALTER TABLE file_info ADD UNIQUE KEY uk_user_filename (url, filename);
```

如果允许同名文件，则删除、下载接口应该改为使用 `file_id` 或记录 `id`，不要继续只靠 `username + filename` 定位。

## 风险点

1. SOCI 是同步库  
   当前项目依赖协程和 hook 机制。如果 SOCI 内部调用的 MySQL client socket 操作没有被 hook 覆盖，数据库请求仍可能阻塞 worker 线程。迁移前需要压测验证。

2. 异常处理方式不同  
   SOCI 通常通过异常报告 SQL 错误。业务层需要统一 catch，并转换成当前接口错误码。

3. 日期时间类型映射需要验证  
   当前代码使用 `time_t`。迁移时要明确 `TIMESTAMP NULL`、`CURRENT_TIMESTAMP` 和空值怎么转换。

4. 连接池语义要重新确认  
   SOCI 自带 `connection_pool`，但当前项目需要协程等待、超时和日志。第一版可以简单封装，后续再决定是否接回自定义池。

5. 不要同时改太多层  
   servlet、SQL、schema、连接池、FastDFS 删除逻辑不要在同一个阶段一起改。

## 验证清单

每个阶段至少验证：

- 编译通过。
- 注册、登录接口可用。
- 上传、秒传、列表、下载、删除接口可用。
- MySQL 连接失败时返回明确错误，不崩溃。
- wrk 业务压测无明显错误率上升。

Docker 环境建议使用：

```bash
bash scripts/docker_test.sh
bash scripts/docker_e2e.sh
bash scripts/docker_bench_business.sh
```

## 最小落地顺序

推荐先做下面这条最小路径：

1. 增加 SOCI 依赖和 `USE_SOCI_DB` 开关。
2. 写 `SELECT 1` smoke test。
3. 用 SOCI 实现 `GetUserByUsername`。
4. 用 SOCI 实现 `GetFileListByUser`。
5. 修正 `FileInfo` 字段映射。
6. 把秒传路径放进事务。

这条路径能最快验证 SOCI 是否适合当前项目，同时不会一次性推翻现有数据库层。
