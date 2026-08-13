#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
HOST_ROOT="$(git rev-parse --show-toplevel)"
CONTAINER_ROOT="/usr/src/Garnet"

echo "[1/3] 检查并启动容器：$CONTAINER"
docker start "$CONTAINER" >/dev/null 2>&1 || true

echo "[2/3] 同步 WSL 源码到容器"
tar \
    --exclude=".git" \
    --exclude="Player-Data" \
    --exclude="KNN-experiment-res" \
    --exclude="local" \
    --exclude="deps" \
    --exclude="Dockerfile.backup" \
    --exclude="*.o" \
    --exclude="*.x" \
    -C "$HOST_ROOT" \
    -cf - . \
| docker exec -i "$CONTAINER" \
    tar -C "$CONTAINER_ROOT" -xf -

echo "[3/3] 在容器内编译 kona.x"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    make -j8 kona.x
"

echo "[完成] kona.x 编译成功"
