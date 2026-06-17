#!/usr/bin/env bash
set -euo pipefail

docker compose -f docker-compose.dev.yml run --rm --no-deps \
  fiberserver-dev bash -lc \
  'cmake -S . -B build -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" && cmake --build build -j"$(nproc)" && ./build/test'
