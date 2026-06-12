#!/usr/bin/env bash
set -euo pipefail

docker compose -f docker-compose.dev.yml run --rm --no-deps \
  -e USE_SOCI_DB="${USE_SOCI_DB:-OFF}" \
  fiberserver-dev bash -lc \
  'cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUSE_SOCI_DB="${USE_SOCI_DB}" && cmake --build build -j"$(nproc)" && ./build/test'
