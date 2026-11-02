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
PONG_REFERENCE=${PONG_REFERENCE:-devices/pong618/docs/build-reference-6.18.60-CLG277.json}
[[ -f $PONG_REFERENCE ]] || { echo 'Missing cohort reference.' >&2; exit 2; }
PONG_PROFILE=$(python3 - "$PONG_REFERENCE" <<'PYPROFILE'
import json,sys
r=json.load(open(sys.argv[1]));print(r.get('kernel_profile','legacy'))
PYPROFILE
)
PONG_CONFIG_FLAGS=()
case "$PONG_PROFILE" in
    lab) [[ -f common/arch/arm64/configs/pong_lab.fragment ]] || { echo 'Selected cohort requires the LAB Kconfig profile.' >&2; exit 2; }; PONG_CONFIG_FLAGS+=(--defconfig_fragment=//common:pong_lab_defconfig);;
    legacy) ;;
    *) echo 'Unknown kernel profile in cohort reference.' >&2; exit 2;;
esac
PONG_BAZEL=(tools/bazel "--output_root=$PONG_OUT/bazel" --host_jvm_args=-Xmx2048m)
PONG_FLAGS=("--jobs=$PONG_BUILD_JOBS" "--make_jobs=$PONG_BUILD_JOBS" "--local_resources=memory=$PONG_BUILD_MEMORY_MIB" --color=no --curses=no)
"${PONG_BAZEL[@]}" run "${PONG_FLAGS[@]}" "${PONG_CONFIG_FLAGS[@]}" //common:kernel_aarch64_dist -- "--destdir=$PONG_OUT/kernel"
"${PONG_BAZEL[@]}" run "${PONG_FLAGS[@]}" "${PONG_CONFIG_FLAGS[@]}" //devices/pong618:pong_dist -- "--destdir=$PONG_OUT/modules"
python3 devices/pong618/tools/verify_build.py \
    --kernel "$PONG_OUT/kernel" --modules "$PONG_OUT/modules" \
    --reference "$PONG_REFERENCE" \
    --report "$PONG_OUT/build-report.json"
python3 devices/pong618/tools/prepare-modules.py freeze \
    --kernel "$PONG_OUT/kernel" --modules "$PONG_OUT/modules" \
    --reference "$PONG_REFERENCE" --output "$PONG_OUT/frozen"
echo "Build checked. Artifacts: $PONG_OUT. Review README before any hardware packaging."
