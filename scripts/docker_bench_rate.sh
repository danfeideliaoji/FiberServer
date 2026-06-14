#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_BASE_URL="${DOWNLOAD_BASE_URL:-$BASE_URL}"
RATE="${RATE:-100}"
DURATION="${DURATION:-30}"
WORKERS="${WORKERS:-5}"
BENCH_KEEPALIVE="${BENCH_KEEPALIVE:-1}"

python3 - "$BASE_URL" "$DOWNLOAD_BASE_URL" "$RATE" "$DURATION" "$WORKERS" "$BENCH_KEEPALIVE" <<'PY'
import concurrent.futures
import hashlib
import http.client
import json
import statistics
import sys
import threading
import time
import urllib.parse

base_url, download_base_url = sys.argv[1], sys.argv[2]
rate, duration, workers = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
keepalive = sys.argv[6] not in ("0", "false", "False", "off", "OFF")
total_requests = rate * duration

username = "rate" + hashlib.md5(str(time.time_ns()).encode()).hexdigest()[:10]
password = "pass123"
file_name = f"rate-sample-{username}.txt"
file_type = "text/plain"
content = f"fiber rate bench {username}".encode()

thread_local = threading.local()
connection_header = "keep-alive" if keepalive else "close"

def parse_base(url):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "http":
        raise SystemExit("docker_bench_rate.sh currently supports http URLs only")
    return parsed, parsed.port or 80

base, base_port = parse_base(base_url)
download_base, download_port = parse_base(download_base_url)

def target(path, params=None):
    encoded = urllib.parse.urlencode(params or {})
    return path + (("?" + encoded) if encoded else "")

def connection_key(parsed, port):
    return (parsed.hostname, port)

def get_connection(parsed, port):
    conns = getattr(thread_local, "conns", None)
    if conns is None:
        conns = {}
        thread_local.conns = conns
    key = connection_key(parsed, port)
    conn = conns.get(key)
    if conn is None:
        conn = http.client.HTTPConnection(parsed.hostname, port, timeout=15)
        conns[key] = conn
    return conn

def close_connection(parsed, port):
    conns = getattr(thread_local, "conns", None)
    if not conns:
        return
    conn = conns.pop(connection_key(parsed, port), None)
    if conn is not None:
        conn.close()

def http_request(parsed, port, method, path, body=None, headers=None):
    conn = get_connection(parsed, port) if keepalive else http.client.HTTPConnection(parsed.hostname, port, timeout=15)
    try:
        conn.request(method, path, body=body, headers=headers or {"Connection": connection_header})
        response = conn.getresponse()
        data = response.read()
        return response.status, data, dict(response.getheaders())
    except Exception:
        if keepalive:
            close_connection(parsed, port)
        raise
    finally:
        if not keepalive:
            conn.close()

def request_json(method, path, payload):
    return http_request(
        base,
        base_port,
        method,
        path,
        json.dumps(payload).encode(),
        {"Content-Type": "application/json", "Connection": connection_header},
    )

def expect_code(name, status, data, code=0):
    if status != 200:
        raise SystemExit(f"{name} failed: status={status} body={data[:200]!r}")
    parsed = json.loads(data.decode())
    if parsed.get("code") != code:
        raise SystemExit(f"{name} failed: expected code={code}, body={parsed}")
    return parsed

expect_code("register", *request_json("POST", "/api/register", {
    "username": username,
    "password": password,
    "nickname": username,
})[:2])
expect_code("login", *request_json("POST", "/api/login", {
    "user": username,
    "pwd": password,
})[:2])

sample_md5 = hashlib.md5(content).hexdigest()
upload_path = target("/api/upload/dirupload", {
    "username": username,
    "md5": sample_md5,
    "filename": file_name,
    "size": len(content),
    "type": file_type,
})
expect_code("setup upload", *http_request(
    base,
    base_port,
    "POST",
    upload_path,
    content,
    {"Content-Type": file_type, "Connection": connection_header},
)[:2])

download_path = target("/api/download", {"user": username, "filename": file_name})

def percentile(values, pct):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round((pct / 100) * (len(ordered) - 1))))
    return ordered[index]

def ok_json_code(status, data):
    if status != 200:
        return False
    try:
        return json.loads(data.decode()).get("code") == 0
    except Exception:
        return False

def make_request(index):
    kind = index % 4
    if kind == 0:
        status, data, _ = http_request(base, base_port, "GET", "/api/status",
                                       headers={"Connection": connection_header})
        return "status", status == 200 and ok_json_code(status, data), status
    if kind == 1:
        status, data, _ = request_json("POST", "/api/login", {"user": username, "pwd": password})
        return "login", ok_json_code(status, data), status
    if kind == 2:
        status, data, _ = request_json("POST", "/api/myfiles", {"username": username})
        return "myfiles", ok_json_code(status, data), status
    status, _, _ = http_request(download_base, download_port, "GET", download_path,
                                headers={"Connection": connection_header})
    return "download", status == 200, status

def once(index, scheduled_at):
    now = time.perf_counter()
    if scheduled_at > now:
        time.sleep(scheduled_at - now)
    started = time.perf_counter()
    status = 0
    endpoint = "unknown"
    error = None
    try:
        endpoint, ok, status = make_request(index)
        if not ok:
            error = f"unexpected response status={status}"
    except Exception as exc:
        kind = index % 4
        endpoint = ("status", "login", "myfiles", "download")[kind]
        error = f"{type(exc).__name__}: {exc}"
    return {
        "index": index,
        "endpoint": endpoint,
        "status": status,
        "latency": (time.perf_counter() - started) * 1000,
        "error": error,
    }

started = time.perf_counter()
futures = []
with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
    for i in range(total_requests):
        scheduled_at = started + (i / rate)
        futures.append(executor.submit(once, i, scheduled_at))
    results = [f.result() for f in futures]
elapsed = time.perf_counter() - started

latencies = [item["latency"] for item in results]
success = sum(1 for item in results if item["error"] is None)
errors = len(results) - success
print(
    f"rate bench: target_rps={rate} duration_s={duration} workers={workers} "
    f"keepalive={int(keepalive)} requests={len(results)} success={success} "
    f"errors={errors} actual_rps={len(results) / elapsed:.2f}"
)

if errors:
    error_items = [item for item in results if item["error"] is not None]
    grouped = {}
    for item in error_items:
        key = (item["endpoint"], item["error"])
        grouped[key] = grouped.get(key, 0) + 1
    print("errors_by_endpoint:")
    for (endpoint, error), count in sorted(grouped.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {endpoint}: count={count} error={error}")
    print("slow_or_error_samples:")
    for item in sorted(error_items, key=lambda item: item["latency"], reverse=True)[:10]:
        print(
            f"  index={item['index']} endpoint={item['endpoint']} "
            f"status={item['status']} latency_ms={item['latency']:.2f} "
            f"error={item['error']}"
        )
print(
    "overall latency_ms "
    f"avg={statistics.mean(latencies):.2f} "
    f"p50={percentile(latencies, 50):.2f} "
    f"p95={percentile(latencies, 95):.2f} "
    f"p99={percentile(latencies, 99):.2f} "
    f"max={max(latencies):.2f}"
)

for endpoint in ("status", "login", "myfiles", "download"):
    items = [item for item in results if item["endpoint"] == endpoint]
    values = [item["latency"] for item in items]
    endpoint_errors = sum(1 for item in items if item["error"] is not None)
    if not values:
        continue
    print(
        f"{endpoint}: count={len(items)} errors={endpoint_errors} "
        f"avg_ms={statistics.mean(values):.2f} "
        f"p95_ms={percentile(values, 95):.2f} "
        f"p99_ms={percentile(values, 99):.2f} "
        f"max_ms={max(values):.2f}"
    )
PY
