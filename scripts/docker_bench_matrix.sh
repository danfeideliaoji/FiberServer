#!/usr/bin/env bash
set -euo pipefail

REQUESTS_LIST="${REQUESTS_LIST:-30 100}"
CONCURRENCY_LIST="${CONCURRENCY_LIST:-1 5 10}"
UPLOAD_REQUESTS="${UPLOAD_REQUESTS:-5}"
UPLOAD_CONCURRENCY="${UPLOAD_CONCURRENCY:-2}"

for requests in $REQUESTS_LIST; do
  for concurrency in $CONCURRENCY_LIST; do
    echo "=== business bench matrix requests=${requests} concurrency=${concurrency} upload_requests=${UPLOAD_REQUESTS} upload_concurrency=${UPLOAD_CONCURRENCY} ==="
    REQUESTS="$requests" \
      CONCURRENCY="$concurrency" \
      UPLOAD_REQUESTS="$UPLOAD_REQUESTS" \
      UPLOAD_CONCURRENCY="$UPLOAD_CONCURRENCY" \
      bash scripts/docker_bench_business.sh
  done
done
