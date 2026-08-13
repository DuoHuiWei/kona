#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/hecompare"
JOBS="${JOBS:-4}"
OPENFHE_PREFIX="${OPENFHE_PREFIX:-/opt/openfhe}"

cmake \
  -S "${ROOT_DIR}/HECompare" \
  -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${OPENFHE_PREFIX}"

cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
