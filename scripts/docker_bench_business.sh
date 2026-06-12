#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_BASE_URL="${DOWNLOAD_BASE_URL:-$BASE_URL}"
REQUESTS="${REQUESTS:-100}"
CONCURRENCY="${CONCURRENCY:-10}"
UPLOAD_REQUESTS="${UPLOAD_REQUESTS:-20}"
UPLOAD_CONCURRENCY="${UPLOAD_CONCURRENCY:-5}"

python3 - "$BASE_URL" "$DOWNLOAD_BASE_URL" "$REQUESTS" "$CONCURRENCY" "$UPLOAD_REQUESTS" "$UPLOAD_CONCURRENCY" <<'PY'
import concurrent.futures
import hashlib
import http.client
import json
import statistics
import sys
import time
import urllib.parse

base_url, download_base_url = sys.argv[1], sys.argv[2]
requests, concurrency = int(sys.argv[3]), int(sys.argv[4])
upload_requests, upload_concurrency = int(sys.argv[5]), int(sys.argv[6])

username = "bench" + hashlib.md5(str(time.time_ns()).encode()).hexdigest()[:10]
password = "pass123"
file_name = f"bench-sample-{username}.txt"
file_type = "text/plain"
content = f"fiber business bench {username}".encode()

def parse_base(url):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "http":
        raise SystemExit("docker_bench_business.sh currently supports http URLs only")
    return parsed, parsed.port or 80

base, base_port = parse_base(base_url)
download_base, download_port = parse_base(download_base_url)

def target(path, params=None):
    encoded = urllib.parse.urlencode(params or {})
    return path + (("?" + encoded) if encoded else "")

def http_request(parsed, port, method, path, body=None, headers=None):
    conn = http.client.HTTPConnection(parsed.hostname, port, timeout=15)
    try:
        conn.request(method, path, body=body, headers=headers or {"Connection": "close"})
        response = conn.getresponse()
        data = response.read()
        return response.status, data, dict(response.getheaders())
    finally:
        conn.close()

def request_json(method, path, payload):
    body = json.dumps(payload).encode()
    return http_request(
        base,
        base_port,
        method,
        path,
        body,
        {"Content-Type": "application/json", "Connection": "close"},
    )

def expect_code(name, status, data, code=0):
    if status != 200:
        raise SystemExit(f"{name} failed: status={status} body={data[:200]!r}")
    try:
        parsed = json.loads(data.decode())
    except Exception as exc:
        raise SystemExit(f"{name} failed: invalid json: {exc} body={data[:200]!r}")
    if parsed.get("code") != code:
        raise SystemExit(f"{name} failed: expected code={code}, body={parsed}")
    return parsed

expect_code(
    "register",
    *request_json("POST", "/api/register", {"username": username, "password": password, "nickname": username})[:2],
)
expect_code("login", *request_json("POST", "/api/login", {"user": username, "pwd": password})[:2])

sample_md5 = hashlib.md5(content).hexdigest()
upload_path = target(
    "/api/upload/dirupload",
    {
        "username": username,
        "md5": sample_md5,
        "filename": file_name,
        "size": len(content),
        "type": file_type,
    },
)
expect_code(
    "setup upload",
    *http_request(
        base,
        base_port,
        "POST",
        upload_path,
        content,
        {"Content-Type": file_type, "Connection": "close"},
    )[:2],
)
expect_code("setup myfiles", *request_json("POST", "/api/myfiles", {"username": username})[:2])

def percentile(values, pct):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = min(len(ordered) - 1, int(round((pct / 100) * (len(ordered) - 1))))
    return ordered[index]

def run_bench(name, total, workers, make_request, ok):
    def once(index):
        started = time.perf_counter()
        status = 0
        error = None
        try:
            status, data, headers = make_request(index)
            if not ok(status, data, headers):
                error = "unexpected response"
        except Exception as exc:
            error = str(exc)
        return status, (time.perf_counter() - started) * 1000, error

    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(executor.map(once, range(total)))
    duration = time.perf_counter() - started
    latencies = [item[1] for item in results]
    success = sum(1 for _, _, error in results if error is None)
    statuses = {}
    for status, _, _ in results:
        statuses[status] = statuses.get(status, 0) + 1
    print(
        f"{name}: requests={total} concurrency={workers} success={success} "
        f"errors={total - success} statuses={statuses} qps={total / duration:.2f} "
        f"avg_ms={statistics.mean(latencies):.2f} p95_ms={percentile(latencies, 95):.2f} "
        f"p99_ms={percentile(latencies, 99):.2f} max_ms={max(latencies):.2f}"
    )

def ok_json_code(status, data, headers):
    if status != 200:
        return False
    try:
        return json.loads(data.decode()).get("code") == 0
    except Exception:
        return False

download_path = target("/api/download", {"user": username, "filename": file_name})

run_bench(
    "status",
    requests,
    concurrency,
    lambda _: http_request(base, base_port, "GET", "/api/status", headers={"Connection": "close"}),
    ok_json_code,
)
run_bench(
    "login",
    requests,
    concurrency,
    lambda _: request_json("POST", "/api/login", {"user": username, "pwd": password}),
    ok_json_code,
)
run_bench(
    "myfiles",
    requests,
    concurrency,
    lambda _: request_json("POST", "/api/myfiles", {"username": username}),
    ok_json_code,
)
run_bench(
    "download",
    requests,
    concurrency,
    lambda _: http_request(download_base, download_port, "GET", download_path, headers={"Connection": "close"}),
    lambda status, data, headers: status == 200,
)

def make_upload(index):
    payload = f"fiber upload bench {username} {index}".encode()
    md5 = hashlib.md5(payload).hexdigest()
    path = target(
        "/api/upload/dirupload",
        {
            "username": username,
            "md5": md5,
            "filename": f"bench-upload-{username}-{index}.txt",
            "size": len(payload),
            "type": file_type,
        },
    )
    return http_request(
        base,
        base_port,
        "POST",
        path,
        payload,
        {"Content-Type": file_type, "Connection": "close"},
    )

run_bench("direct_upload", upload_requests, upload_concurrency, make_upload, ok_json_code)
print(f"business bench complete: user={username}")
PY
