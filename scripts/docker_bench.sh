#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
BENCH_PATH="${BENCH_PATH:-/api/status}"
REQUESTS="${REQUESTS:-200}"
CONCURRENCY="${CONCURRENCY:-20}"
BENCH_KEEPALIVE="${BENCH_KEEPALIVE:-1}"

python3 - "$BASE_URL" "$BENCH_PATH" "$REQUESTS" "$CONCURRENCY" "$BENCH_KEEPALIVE" <<'PY'
import concurrent.futures
import http.client
import statistics
import sys
import threading
import time
import urllib.parse

base_url, bench_path = sys.argv[1], sys.argv[2]
requests, concurrency = int(sys.argv[3]), int(sys.argv[4])
keepalive = sys.argv[5] not in ("0", "false", "False", "off", "OFF")
url = urllib.parse.urljoin(base_url.rstrip("/") + "/", bench_path.lstrip("/"))
parsed = urllib.parse.urlparse(url)
port = parsed.port or (443 if parsed.scheme == "https" else 80)
target = parsed.path or "/"
if parsed.query:
    target += "?" + parsed.query

if parsed.scheme != "http":
    raise SystemExit("docker_bench.sh currently supports http URLs only")

thread_local = threading.local()
connection_header = "keep-alive" if keepalive else "close"

def get_connection():
    conn = getattr(thread_local, "conn", None)
    if conn is None:
        conn = http.client.HTTPConnection(parsed.hostname, port, timeout=10)
        thread_local.conn = conn
    return conn

def close_connection():
    conn = getattr(thread_local, "conn", None)
    if conn is not None:
        conn.close()
        thread_local.conn = None

def request_once(_):
    start = time.perf_counter()
    conn = get_connection() if keepalive else http.client.HTTPConnection(parsed.hostname, port, timeout=10)
    try:
        conn.request("GET", target, headers={"Connection": connection_header})
        response = conn.getresponse()
        response.read()
        elapsed_ms = (time.perf_counter() - start) * 1000
        return response.status, elapsed_ms, None
    except Exception as exc:
        if keepalive:
            close_connection()
        elapsed_ms = (time.perf_counter() - start) * 1000
        return 0, elapsed_ms, str(exc)
    finally:
        if not keepalive:
            conn.close()

started = time.perf_counter()
with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
    results = list(executor.map(request_once, range(requests)))
duration = time.perf_counter() - started

latencies = [item[1] for item in results]
success = sum(1 for status, _, error in results if status == 200 and error is None)
errors = requests - success
statuses = {}
for status, _, _ in results:
    statuses[status] = statuses.get(status, 0) + 1

def percentile(values, pct):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round((pct / 100) * (len(ordered) - 1))))
    return ordered[index]

print(f"bench url={url} requests={requests} concurrency={concurrency} keepalive={int(keepalive)}")
print(f"success={success} errors={errors} statuses={statuses}")
print(f"duration_s={duration:.3f} qps={requests / duration:.2f}")
print(
    "latency_ms "
    f"avg={statistics.mean(latencies):.2f} "
    f"p50={percentile(latencies, 50):.2f} "
    f"p95={percentile(latencies, 95):.2f} "
    f"p99={percentile(latencies, 99):.2f} "
    f"max={max(latencies):.2f}"
)
PY
