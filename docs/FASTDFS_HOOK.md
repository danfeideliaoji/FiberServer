# FastDFS Hook Verification

This note records the FastDFS syscall hook test on branch `fastdfs-hook-test`.

## Conclusion

Direct FastDFS syscall hook is usable only when the FastDFS client connection pool is enabled:

```conf
use_connection_pool=true
```

With `use_connection_pool=false`, the FastDFS C client can return the same global tracker `ConnectionInfo` slot to concurrent coroutine requests. After the hook layer yields inside `connect`, `recv`, `send`, `read`, or `write`, another coroutine may reuse the same socket and register the same fd event again. In this project that showed up as duplicate `IOManager::addEvent` waiters on the same fd, followed by upload 502s or process aborts.

With `use_connection_pool=true`, concurrent uploads get distinct pooled `ConnectionInfo` objects and sockets, so the duplicate fd waiter issue was not reproduced.

## Verification

Environment:

- Branch: `fastdfs-hook-test`
- FastDFS client config: `docker/fdfs/client.conf`
- App build: Release Docker build
- Benchmark script: `scripts/docker_bench_business.sh`

Observed with `use_connection_pool=false`:

| Scenario | Result |
| --- | --- |
| Direct upload, 40 requests / 20 concurrency | Most requests returned 502 |
| Direct upload, 200 requests / 50 concurrency | App crashed under duplicate fd event registration |

Observed with `use_connection_pool=true`:

| Scenario | Result |
| --- | --- |
| Direct upload, 40 requests / 20 concurrency | 40 / 40 success |
| Direct upload, 200 requests / 50 concurrency, keep-alive off | 200 / 200 success, avg 97.43ms, p95 126.71ms, p99 133.67ms |
| Direct upload, 200 requests / 50 concurrency, keep-alive on | 199 / 200 success; the single failure was an nginx 499 client timeout, not a 502 or app crash |

The final verification after removing temporary trace logs passed the Docker build and pressure run. No FastDFS hook assertion or process crash was observed with the connection pool enabled.

## Operational Rule

Keep `docker/fdfs/client.conf` at:

```conf
use_connection_pool=true
```

Do not switch it back to `false` while FastDFS calls run under the hook-enabled IOManager. If the connection pool must be disabled, FastDFS calls should be isolated in blocking worker threads or otherwise serialized per FastDFS connection.
