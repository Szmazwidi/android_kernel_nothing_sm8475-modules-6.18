# Nothing Phone (2): android17-6.18.60 vendor modules

**6.18.60 hardware qualification failed:** the first source-built candidate
passed compile/CRC/signature/VM checks but could not complete Android boot.
The release recipe selected the previously rejected heap974 exporter rather
than accepted heap982, losing the `qcom,system` name, userspace mmap and lazy
DMA mapping. The phone was restored to exact CLS38 / 6.18.59 without a reset.
This branch now selects the unchanged accepted heap982 source and checks its
runtime interfaces during artifact verification. A new complete build,
signing/VM qualification and hardware test are still required before use.
See [runtime regression](docs/runtime-regression-6.18.60.md). The remote default
and preserved 6.18.59 branch remain unchanged.


Source release branch: `pong-a17-6.18.60`. The older `pong-a17-6.18.32` branch
is preserved as the historical image-734 checkpoint. This branch extends it
with the current port source snapshots: **178 vendor module outputs** plus
**84 reference GKI modules** built with the companion kernel.

Kernel: [android_kernel_nothing_sm8475-6.18](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/tree/pong-a17-6.18.60).
The kernel retains Linux/ACK/stable history, the U59 integration and its KMI
reverts. Its 6.18.59 release string does not imply an unmodified mainline tree.

For the captured complete ROM recipe and the actual AIDL/HIDL selection, see
[ROM source state](docs/rom/README.md) and [HAL interfaces](docs/hal-interfaces.md).

## Build a pinned source set

The repositories are private at present. Configure your normal GitHub Git
credentials first; do not put access tokens into commands, manifests or commits.
The final XML pins the verified source commits. A subsequent documentation-only
commit stores that lock without changing the compiled source inputs.

```sh
git clone --branch pong-a17-6.18.60 --single-branch \
  https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18.git pong-source
mkdir pong-workspace && cd pong-workspace
repo init -u https://android.googlesource.com/kernel/manifest -b common-android17-6.18
cp ../pong-source/docs/manifest/pong618-6.18.60.xml .repo/manifests/pong618.xml
repo init -m pong618.xml
repo sync -c -j4
bash devices/pong618/tools/build-pong.sh
```

Run the build script from the workspace root. It builds the kernel and the whole
vendor output set, then checks output coverage, ELF identity, dependencies and imported-symbol
CRCs of **all 262 selected modules**, including GKI providers. Only actual selected
ELF exports and built-in kernel exports can satisfy imports; stale DDK cache
metadata cannot make the check pass. Set `PONG_BUILD_JOBS` and `PONG_BUILD_MEMORY_MIB` to control resources;
`PONG_OUT` selects the output directory. No old `.ko`, `Module.symvers`, private
key, phone access or `/mnt/storage` work folder is needed as an input.

The script is an ordinary wrapper around:

```sh
tools/bazel run //common:kernel_aarch64_dist -- --destdir=out/pong/kernel
tools/bazel run //devices/pong618:pong_dist -- --destdir=out/pong/modules
```

See `docs/build-reference-6.18.60.json` for every selected vendor target and GKI
filename. `build/providers.bzl` obtains symbol providers from source builds.
The original stage-based package paths are retained for traceability and to
avoid gratuitous source-path/BTF changes.

## Before packaging for hardware

Compilation is distinct from runtime qualification. The fresh-build report
records what was actually tested on the host; it does not assert that these
new local-key artifacts were flashed. The laboratory runtime reference is CLS38 (BT_EN kept asserted while WLAN is
powered, WLAN cold boot calibration off, qrtr-mhi without MHISTATUS reads in
suspend_late, cnss2 non-DRV system suspend). Current source snapshots can include
ongoing driver work; inspect the source manifest and commit history.

A fresh key and a changed BTF layout mean the old laboratory payload allowlist
will not admit newly built modules. Generate a list from the complete first-pass
module set, keep that module set, rebuild only the kernel, then sign with its key:

```sh
# build-pong.sh creates frozen/ with only the selected 178+84 payloads.
cp out/pong/frozen/pong251-module-policy.h common/kernel/module/pong251-module-policy.h
PONG_RELEASE_OUT="$(realpath out/pong)"
tools/bazel "--output_root=$PONG_RELEASE_OUT/bazel" --host_jvm_args=-Xmx2048m run \
  --jobs=4 --make_jobs=4 --local_resources=memory=7168 \
  //common:kernel_aarch64_dist -- "--destdir=$PONG_RELEASE_OUT/kernel-final"
python3 devices/pong618/tools/prepare-modules.py sign \
  --frozen out/pong/frozen --image out/pong/kernel-final/Image \
  --key bazel-bin/common/kernel_aarch64/certs/signing_key.pem \
  --cert bazel-bin/common/kernel_aarch64/certs/signing_key.x509 \
  --output out/pong/signed
```

The signing tool rechecks all 262 frozen modules against the final kernel ABI,
requires its signature-force configuration, and checks all selected payload
entries and the public certificate
inside the final Image, then verifies every signature cryptographically against
that certificate. A different image/key or changed frozen file is rejected.
Keep the private key outside Git and other shared artifacts. The unmodified
source policy can be restored after the final Image is saved; retain the generated
policy and frozen manifest alongside the build evidence.

Use the first-pass module files when signing/packaging, not a new DDK rebuild
against the changed final vmlinux BTF. Native signatures remain required. The source configuration enables
`CONFIG_MODULE_SIG_FORCE=y`, automatic GKI signing and SHA-256. The old
sign-file compatibility check incorrectly assumed every PKCS#7 fallback used
SHA-1; BoringSSL actually uses RSA/SHA-256. The narrow BoringSSL-specific fix
preserves the legacy OpenSSL fallback and keeps the GKI KMI checks enabled.
Final frozen modules also use independently verified host OpenSSL CMS SHA-256.
`CONFIG_MODULE_ALLOW_BTF_MISMATCH` is an existing requirement of this two-pass
workflow. The image tools support signing with the builder's final key.

`tools/images/` remains the **historical image-734 packaging recipe**. Its lists
and userspace startup profile are not the current CLS38 profile. Do not present
that old recipe as a tested 262-module Android image. Building a complete ROM
also needs a matching userspace/firmware, device tree, startup ordering and
compatibility policy; proprietary userspace blobs are not supplied here.
There is no automatic flashing step. Physical write guards and RAM hot-add are
specific to the tested hardware/partition geometry; do not assume an 8 GB SKU
has the 12 GB hot-add memory ranges.

## Runtime reference and outstanding work

On the development phone, the port has booted native Android with display/touch,
audio, camera preview/JPEG, sensors, mobile data, CPU frequency scaling and the
12 GB resource group. These are stage-specific observations, not a guarantee
that every fresh build or ROM configuration has all features enabled.

CLS37/38 address the observed qrtr-mhi suspend MMIO hang and WLAN DRV link loss;
long-duration/low-power qualification remains open. Other remaining areas include
BT startup and interoperability, CDSP listener retries,
an OEM telephony-class dependency in the ROM, IOMMU fault-handler API adaptation,
UBWC compression and low-power qualification. NFC controller ON is verified;
payment/card interoperability and other long-duration tests remain separate.
Audio startup, module autoload switches and the companion exact-image verifier
must be kept consistent with the deployed init RC; a new RC hash without its
matching verifier previously disabled the RAM/NFC/Glyph/haptic group.

## Authorship, history and licenses

Existing upstream history remains intact. New package imports retain their
original notices and exact donor mappings, separately from build/integration
changes. Full donor history is not imported for every new package path; see
[the provenance guide](docs/provenance-guide.md). `docs/provenance.json` records
verbatim, modified and project-local sources with exact donor revisions. Files
whose donor is not confidently mapped are explicitly labelled; they are not
claimed as newly authored upstream code. Source/build adaptation was prepared
by Szmazwidi with OpenAI Codex and Anthropic Claude assistance.

Per-file SPDX and copyright notices take precedence. `LICENSES/` and `COPYING`
are copied verbatim from the kernel license inventory; this is not a blanket
claim that every build tool is GPL-2.0. No private signing keys, personal phone
backups, runtime private logs or proprietary DSP/userspace firmware are included.
