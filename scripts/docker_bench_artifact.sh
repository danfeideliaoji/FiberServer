#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_HEADER_BASE_URL="${DOWNLOAD_HEADER_BASE_URL:-$BASE_URL}"
REQUESTS="${REQUESTS:-200}"
CONCURRENCY="${CONCURRENCY:-20}"
UPLOAD_REQUESTS="${UPLOAD_REQUESTS:-5}"
UPLOAD_CONCURRENCY="${UPLOAD_CONCURRENCY:-2}"
BENCH_KEEPALIVE="${BENCH_KEEPALIVE:-1}"
RUN_ID="bench$(date +%s%N)"

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "missing required command: $1" >&2
        exit 1
    }
}

require_cmd python3

python3 - "$BASE_URL" "$DOWNLOAD_HEADER_BASE_URL" "$REQUESTS" "$CONCURRENCY" "$UPLOAD_REQUESTS" "$UPLOAD_CONCURRENCY" "$BENCH_KEEPALIVE" "$RUN_ID" <<'PY'
import concurrent.futures
import hashlib
import http.client
import json
import statistics
import sys
import threading
import time
import urllib.parse

(
    base_url,
    download_header_base_url,
    requests_s,
    concurrency_s,
    upload_requests_s,
    upload_concurrency_s,
    keepalive_s,
    run_id,
) = sys.argv[1:]

requests_count = int(requests_s)
concurrency = int(concurrency_s)
upload_requests = int(upload_requests_s)
upload_concurrency = int(upload_concurrency_s)
keepalive = keepalive_s not in ("0", "false", "False", "off", "OFF")

project = f"artifact-bench-{run_id}"
token = f"token-{run_id}"
version = "1.0.0"
build_no = f"build-{run_id}"
artifact_name = f"bench-{run_id}.txt"
artifact_type = "text/plain"
content = f"FiberServer artifact benchmark seed {run_id}\n".encode()
checksum = hashlib.md5(content).hexdigest()

thread_local = threading.local()


def parse_base(url):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "http":
        raise SystemExit("docker_bench_artifact.sh currently supports http URLs only")
    return parsed, parsed.port or 80


base_parsed, base_port = parse_base(base_url)
header_parsed, header_port = parse_base(download_header_base_url)


def path_with_query(path, params=None):
    if not params:
        return path
    return path + "?" + urllib.parse.urlencode(params)


def conn_key(parsed, port):
    return f"{parsed.hostname}:{port}"


def get_connection(parsed, port):
    key = conn_key(parsed, port)
    conns = getattr(thread_local, "conns", None)
    if conns is None:
        conns = {}
        thread_local.conns = conns
    conn = conns.get(key)
    if conn is None:
        conn = http.client.HTTPConnection(parsed.hostname, port, timeout=15)
        conns[key] = conn
    return conn


def close_connection(parsed, port):
    conns = getattr(thread_local, "conns", None)
    if not conns:
        return
    key = conn_key(parsed, port)
    conn = conns.pop(key, None)
    if conn is not None:
        conn.close()


def request(parsed, port, method, path, body=None, headers=None, expect=(200,)):
    if headers is None:
        headers = {}
    headers = dict(headers)
    headers.setdefault("Connection", "keep-alive" if keepalive else "close")
    conn = get_connection(parsed, port) if keepalive else http.client.HTTPConnection(parsed.hostname, port, timeout=15)
    try:
        conn.request(method, path, body=body, headers=headers)
        response = conn.getresponse()
        data = response.read()
        if response.status not in expect:
            raise RuntimeError(f"{method} {path} returned HTTP {response.status}: {data[:200]!r}")
        return response.status, dict(response.getheaders()), data
    except Exception:
        if keepalive:
            close_connection(parsed, port)
        raise
    finally:
        if not keepalive:
            conn.close()


def request_json(method, path, payload, headers=None, expect=(200,)):
    body = json.dumps(payload, separators=(",", ":")).encode()
    merged = {"Content-Type": "application/json"}
    if headers:
        merged.update(headers)
    status, response_headers, data = request(base_parsed, base_port, method, path, body=body, headers=merged, expect=expect)
    parsed = json.loads(data.decode() or "{}")
    return status, response_headers, parsed


def assert_code(name, payload, expected):
    actual = payload.get("code")
    if actual != expected:
        raise RuntimeError(f"{name} expected code {expected}, got {actual}: {payload}")


auth_header = {"Authorization": f"Bearer {token}"}

# 先准备一个稳定可读的制品，后续读接口都围绕它压测。
_, _, token_payload = request_json("POST", "/api/artifacts/token", {"project_name": project, "token": token})
assert_code("token create", token_payload, 0)

precheck_payload = {
    "project_name": project,
    "version": version,
    "build_no": build_no,
    "artifact_name": artifact_name,
    "checksum": checksum,
    "size": len(content),
    "artifact_type": artifact_type,
}
_, _, precheck = request_json("POST", "/api/artifacts/precheck", precheck_payload, headers=auth_header)
if precheck.get("code") not in (0, 1):
    raise RuntimeError(f"precheck expected code 0 or 1, got {precheck}")

upload_path = path_with_query("/api/artifacts/upload/direct", precheck_payload)
_, _, upload_data = request(base_parsed, base_port, "POST", upload_path, body=content, headers={**auth_header, "Content-Type": artifact_type})
upload_payload = json.loads(upload_data.decode() or "{}")
if upload_payload.get("code") not in (0,):
    raise RuntimeError(f"seed upload expected code 0, got {upload_payload}")

download_path = path_with_query(
    "/api/artifacts/download",
    {"project_name": project, "version": version, "build_no": build_no, "artifact_name": artifact_name},
)

benchmarks = [
    ("status", base_parsed, base_port, "GET", "/api/status", None, None, (200,)),
    ("artifact_list", base_parsed, base_port, "POST", "/api/artifacts/list", json.dumps({"project_name": project}).encode(), {"Content-Type": "application/json"}, (200,)),
    ("artifact_latest", base_parsed, base_port, "GET", path_with_query("/api/artifacts/latest", {"project_name": project}), None, None, (200,)),
    ("artifact_versions", base_parsed, base_port, "GET", path_with_query("/api/artifacts/versions", {"project_name": project}), None, None, (200,)),
    ("artifact_builds", base_parsed, base_port, "GET", path_with_query("/api/artifacts/builds", {"project_name": project, "version": version}), None, None, (200,)),
    ("artifact_download_header", header_parsed, header_port, "HEAD", download_path, None, None, (200,)),
]


def percentile(values, pct):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round((pct / 100) * (len(ordered) - 1))))
    return ordered[index]


def run_request(task):
    name, parsed, port, method, path, body, headers, expect = task
    start = time.perf_counter()
    try:
        status, _, data = request(parsed, port, method, path, body=body, headers=headers, expect=expect)
        if name != "artifact_download_header" and data:
            payload = json.loads(data.decode() or "{}")
            if payload.get("code", 0) != 0:
                raise RuntimeError(f"business code {payload.get('code')}: {payload}")
        return status, (time.perf_counter() - start) * 1000, None
    except Exception as exc:
        return 0, (time.perf_counter() - start) * 1000, str(exc)


def run_benchmark(task):
    name = task[0]
    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        results = list(executor.map(run_request, [task] * requests_count))
    duration = time.perf_counter() - started
    latencies = [item[1] for item in results]
    success = sum(1 for status, _, error in results if status in task[7] and error is None)
    errors = len(results) - success
    statuses = {}
    for status, _, _ in results:
        statuses[status] = statuses.get(status, 0) + 1
    print(f"{name}: requests={requests_count} concurrency={concurrency} keepalive={int(keepalive)} success={success} errors={errors} statuses={statuses}")
    print(
        f"{name}: duration_s={duration:.3f} qps={len(results) / duration:.2f} "
        f"avg_ms={statistics.mean(latencies):.2f} "
        f"p50_ms={percentile(latencies, 50):.2f} "
        f"p95_ms={percentile(latencies, 95):.2f} "
        f"p99_ms={percentile(latencies, 99):.2f} "
        f"max_ms={max(latencies):.2f}"
    )
    if errors:
        first_error = next((error for _, _, error in results if error), "")
        print(f"{name}: first_error={first_error}")


print(f"artifact bench project={project} base_url={base_url} download_header_base_url={download_header_base_url}")
for task in benchmarks:
    run_benchmark(task)


def run_upload_once(i):
    upload_name = f"bench-upload-{run_id}-{i}.txt"
    upload_content = f"FiberServer artifact upload benchmark {run_id} #{i}\n".encode()
    upload_checksum = hashlib.md5(upload_content).hexdigest()
    params = {
        "project_name": project,
        "version": "upload",
        "build_no": str(i),
        "artifact_name": upload_name,
        "checksum": upload_checksum,
        "size": len(upload_content),
        "artifact_type": "text/plain",
    }
    path = path_with_query("/api/artifacts/upload/direct", params)
    start = time.perf_counter()
    try:
        status, _, data = request(base_parsed, base_port, "POST", path, body=upload_content, headers={**auth_header, "Content-Type": "text/plain"})
        payload = json.loads(data.decode() or "{}")
        if status != 200 or payload.get("code") != 0:
            raise RuntimeError(f"status={status} payload={payload}")
        return status, (time.perf_counter() - start) * 1000, None
    except Exception as exc:
        return 0, (time.perf_counter() - start) * 1000, str(exc)


if upload_requests > 0:
    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=upload_concurrency) as executor:
        results = list(executor.map(run_upload_once, range(upload_requests)))
    duration = time.perf_counter() - started
    latencies = [item[1] for item in results]
    success = sum(1 for status, _, error in results if status == 200 and error is None)
    errors = len(results) - success
    statuses = {}
    for status, _, _ in results:
        statuses[status] = statuses.get(status, 0) + 1
    print(f"artifact_upload_direct: requests={upload_requests} concurrency={upload_concurrency} keepalive={int(keepalive)} success={success} errors={errors} statuses={statuses}")
    print(
        f"artifact_upload_direct: duration_s={duration:.3f} qps={len(results) / duration:.2f} "
        f"avg_ms={statistics.mean(latencies):.2f} "
        f"p50_ms={percentile(latencies, 50):.2f} "
        f"p95_ms={percentile(latencies, 95):.2f} "
        f"p99_ms={percentile(latencies, 99):.2f} "
        f"max_ms={max(latencies):.2f}"
    )
    if errors:
        first_error = next((error for _, _, error in results if error), "")
        print(f"artifact_upload_direct: first_error={first_error}")
PY
