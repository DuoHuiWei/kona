#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
HOST_ROOT="$(git rev-parse --show-toplevel)"
CONTAINER_ROOT="/usr/src/Garnet"

echo "[1/3] 检查并启动容器：$CONTAINER"
docker start "$CONTAINER" >/dev/null 2>&1 || true

echo "[2/3] 同步 WSL 源码到容器"
# Archive file/symlink entries explicitly rather than the repository directory.
# Desktop sandbox mounts or unrelated directory-entry changes can update '.'
# while tar reads it, aborting otherwise unchanged-source benchmark launches.
# No directory metadata is archived; changes to an actual archived file still
# make tar fail. Keep volatile agent mounts out of the build environment.
KONA_SOURCE_LIST="$(mktemp "${TMPDIR:-/tmp}/kona-source-list.XXXXXX")"
trap 'rm -f "$KONA_SOURCE_LIST"' EXIT
(
    cd "$HOST_ROOT"
    find . \( -name .git -o -name Player-Data -o -name KNN-experiment-res \
        -o -name local -o -name deps -o -name .codex -o -name .agents \
        -o -name .aws -o -name Dockerfile.backup -o -name '*.o' -o -name '*.x' \) \
        -prune -o \( -type f -o -type l \) -print0
) > "$KONA_SOURCE_LIST"
tar \
    -C "$HOST_ROOT" \
    --null --verbatim-files-from --no-recursion \
    -cf - -T "$KONA_SOURCE_LIST" \
| docker exec -i "$CONTAINER" \
    tar -C "$CONTAINER_ROOT" -xf -

echo "[3/3] 在容器内编译 kona.x"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    make -j8 kona.x
"

echo "[完成] kona.x 编译成功"
