#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Build artifacts only. No phone access, flashing, or removal of lab guards.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
    echo 'Run from the Kleaf workspace root. Optional: PONG_OUT, PONG_BUILD_JOBS, PONG_BUILD_MEMORY_MIB.'
    exit 0
fi
[[ $# == 0 && -f common/Makefile && -e tools/bazel && -f devices/pong618/BUILD.bazel ]] || {
    echo 'Run from the pinned Kleaf workspace root; see README.md.' >&2; exit 2;
}
PONG_OUT=${PONG_OUT:-out/pong}
PONG_BUILD_JOBS=${PONG_BUILD_JOBS:-4}
PONG_BUILD_MEMORY_MIB=${PONG_BUILD_MEMORY_MIB:-7168}
[[ $PONG_BUILD_JOBS =~ ^[1-9][0-9]*$ && $PONG_BUILD_MEMORY_MIB =~ ^[1-9][0-9]*$ ]] || exit 2
mkdir -p "$PONG_OUT"
PONG_OUT=$(cd "$PONG_OUT" && pwd)
PONG_BAZEL=(tools/bazel "--output_root=$PONG_OUT/bazel" --host_jvm_args=-Xmx2048m)
PONG_FLAGS=("--jobs=$PONG_BUILD_JOBS" "--make_jobs=$PONG_BUILD_JOBS" "--local_resources=memory=$PONG_BUILD_MEMORY_MIB" --color=no --curses=no)
"${PONG_BAZEL[@]}" run "${PONG_FLAGS[@]}" //common:kernel_aarch64_dist -- "--destdir=$PONG_OUT/kernel"
"${PONG_BAZEL[@]}" run "${PONG_FLAGS[@]}" //devices/pong618:pong_dist -- "--destdir=$PONG_OUT/modules"
python3 devices/pong618/tools/verify_build.py \
    --kernel "$PONG_OUT/kernel" --modules "$PONG_OUT/modules" \
    --vendor-symvers bazel-bin/devices/pong618 \
    --reference devices/pong618/docs/build-reference-6.18.59.json \
    --report "$PONG_OUT/build-report.json"
python3 devices/pong618/tools/gen-module-policy.py "$PONG_OUT/kernel" "$PONG_OUT/modules" > "$PONG_OUT/pong251-module-policy.h"
echo "Build checked. Artifacts: $PONG_OUT. Review README before any hardware packaging."
