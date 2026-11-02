#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Complete source build, immutable payload policy, signing and diskless VM test.
# Run from the pinned Kleaf workspace root. No phone access or image flashing.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
    echo 'Run from the pinned Kleaf workspace root with PONG_QEMU=/path/to/qemu-system-aarch64.'
    echo 'Optional: PONG_OUT, PONG_BUILD_JOBS, PONG_BUILD_MEMORY_MIB, PONG_QEMU_DATA, PONG_CLANG, PONG_STEP_TIMEOUT.'
    exit 0
fi
[[ $# == 0 && -f common/Makefile && -x tools/bazel && -f devices/pong618/tools/build-pong.sh ]] || exit 2
PONG_OUT=${PONG_OUT:-out/pong-release}
PONG_BUILD_JOBS=${PONG_BUILD_JOBS:-4}
PONG_BUILD_MEMORY_MIB=${PONG_BUILD_MEMORY_MIB:-7168}
PONG_STEP_TIMEOUT=${PONG_STEP_TIMEOUT:-2700}
PONG_QEMU=${PONG_QEMU:-$(command -v qemu-system-aarch64 || true)}
[[ -n $PONG_QEMU && -x $PONG_QEMU ]] || { echo 'Set PONG_QEMU to a working AArch64 system QEMU before building.' >&2; exit 2; }
[[ $PONG_STEP_TIMEOUT =~ ^[1-9][0-9]*$ && $PONG_BUILD_JOBS =~ ^[1-9][0-9]*$ && $PONG_BUILD_MEMORY_MIB =~ ^[1-9][0-9]*$ ]] || exit 2
mkdir -p "$PONG_OUT"
PONG_OUT=$(cd "$PONG_OUT" && pwd)
PONG_REFERENCE=${PONG_REFERENCE:-devices/pong618/docs/build-reference-6.18.60-CLG277.json}
[[ -f $PONG_REFERENCE ]] || exit 2
PONG_PROFILE=$(python3 - "$PONG_REFERENCE" <<'PYPROFILE'
import json,sys
print(json.load(open(sys.argv[1])).get('kernel_profile','legacy'))
PYPROFILE
)
PONG_CONFIG_FLAGS=()
case "$PONG_PROFILE" in
    lab) [[ -f common/arch/arm64/configs/pong_lab.fragment ]] || exit 2; PONG_CONFIG_FLAGS+=(--defconfig_fragment=//common:pong_lab_defconfig);;
    legacy) ;;
    *) echo 'Unknown cohort profile.' >&2; exit 2;;
esac
export PONG_OUT PONG_BUILD_JOBS PONG_BUILD_MEMORY_MIB PONG_REFERENCE
exec 9>.pong-release-build.lock
flock -n 9 || { echo 'A release build already owns this kernel checkout.' >&2; exit 2; }
# Do not turn an unrelated uncommitted policy edit into an automatic rollback.
[[ -z $(git -C common status --porcelain --untracked-files=no) ]] || { echo 'Commit or preserve kernel changes first.' >&2; exit 2; }
PONG_POLICY=common/kernel/module/pong251-module-policy.h
PONG_APPLIED=0
restore_policy() {
    local rc=$?
    trap - EXIT
    if [[ $PONG_APPLIED == 1 ]]; then
        if cmp -s "$PONG_POLICY" "$PONG_OUT/frozen/pong251-module-policy.h"; then
            cat "$PONG_OUT/original-policy.h" > "$PONG_POLICY"
        else
            echo 'Concurrent policy change detected; refusing to overwrite it.' >&2
            rc=1
        fi
    fi
    if [[ $rc != 0 ]]; then echo 'Build stopped; logs and frozen artifacts retained. No hardware action performed.' >&2; fi
    exit "$rc"
}
trap restore_policy EXIT
PONG_BAZEL=(tools/bazel "--output_root=$PONG_OUT/bazel" --host_jvm_args=-Xmx2048m)
PONG_FLAGS=("--jobs=$PONG_BUILD_JOBS" "--make_jobs=$PONG_BUILD_JOBS" "--local_resources=memory=$PONG_BUILD_MEMORY_MIB" --color=no --curses=no)
timeout --signal=TERM --kill-after=30 "$PONG_STEP_TIMEOUT" \
    bash devices/pong618/tools/build-pong.sh > "$PONG_OUT/firstpass.log" 2>&1
cp "$PONG_POLICY" "$PONG_OUT/original-policy.h"
cp "$PONG_OUT/frozen/pong251-module-policy.h" "$PONG_POLICY"
PONG_APPLIED=1
timeout --signal=TERM --kill-after=30 "$PONG_STEP_TIMEOUT" \
    "${PONG_BAZEL[@]}" run "${PONG_FLAGS[@]}" "${PONG_CONFIG_FLAGS[@]}" //common:kernel_aarch64_dist -- \
    "--destdir=$PONG_OUT/kernel-final" > "$PONG_OUT/final-kernel.log" 2>&1
python3 devices/pong618/tools/prepare-modules.py sign \
    --frozen "$PONG_OUT/frozen" --image "$PONG_OUT/kernel-final/Image" \
    --key bazel-bin/common/kernel_aarch64/certs/signing_key.pem \
    --cert bazel-bin/common/kernel_aarch64/certs/signing_key.x509 \
    --output "$PONG_OUT/signed" > "$PONG_OUT/signatures.log" 2>&1
PONG_VM=(--qemu "$PONG_QEMU" --kernel "$PONG_OUT/kernel-final/Image" \
    --signed "$PONG_OUT/signed" --frozen "$PONG_OUT/frozen" \
    --key bazel-bin/common/kernel_aarch64/certs/signing_key.pem \
    --cert bazel-bin/common/kernel_aarch64/certs/signing_key.x509 --output "$PONG_OUT/VM")
[[ -z ${PONG_QEMU_DATA:-} ]] || PONG_VM+=(--qemu-data "$PONG_QEMU_DATA")
[[ -z ${PONG_CLANG:-} ]] || PONG_VM+=(--clang "$PONG_CLANG")
python3 devices/pong618/tools/test-module-guards.py "${PONG_VM[@]}" > "$PONG_OUT/VM.log" 2>&1
python3 - "$PONG_OUT" "$PONG_REFERENCE" <<'PY'
import hashlib,json,pathlib,sys
out=pathlib.Path(sys.argv[1]);bank=json.loads((out/'signed/manifest.json').read_text());vm=json.loads((out/'VM/report.json').read_text())
reference=json.loads(pathlib.Path(sys.argv[2]).read_text());expected=set(reference['vendor'])|set(reference['gki']);declared=reference.get('expected_counts',{'total':262})['total']
assert len(expected)==declared and set(bank['modules'])==expected and bank['count']==declared and bank['final_ABI_verified']and bank['signature_force_config_verified']and vm['status']=='PASS'
image=hashlib.sha256((out/'kernel-final/Image').read_bytes()).hexdigest();assert bank['final_kernel_sha256']==vm['kernel_sha256']==image
(out/'release-report.json').write_text(json.dumps(dict(status='HOST_BUILD_SIGNATURES_AND_VM_PASS',kernel_sha256=image,modules=declared,cohort_reference=sys.argv[2],hardware_tested=False,phone_operations=0),indent=2)+'\n')
PY
echo "Complete source artifacts verified: $PONG_OUT/release-report.json"
