# Nothing Phone (2), Android 17 and Linux 6.18.60

This source candidate selects **293 modules**, comprising 209 vendor modules
and 84 GKI modules, with 100 modules in the first-stage vendor ramdisk.
It contains the CLG43 source cohort. OTG/UCSI and FSA4480 development remains
on separate branches.

The kernel and module repositories preserve ACK, stable and vendor history.
Existing source notices, donor commits and local adaptation scope are documented
in [provenance](docs/provenance-guide.md). The kernel release is an Android
port with device support changes.

## Build the community candidate

Install the normal Android kernel host dependencies, `repo` and AArch64 system
QEMU. Keep the support clone outside the Kleaf workspace.

```sh
git clone --branch pong-a17-6.18.60-community-clg43 --single-branch \
  https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18.git pong-source
mkdir pong-workspace && cd pong-workspace
repo init -u https://android.googlesource.com/kernel/manifest -b common-android17-6.18
cp ../pong-source/docs/manifest/pong618-6.18.60-community.xml .repo/manifests/pong618.xml
repo init -m pong618.xml
repo sync -c -j4
PONG_REFERENCE=devices/pong618/docs/build-reference-6.18.60-community.json \
PONG_QEMU=/absolute/path/to/qemu-system-aarch64 \
PONG_OUT=out/pong-community \
bash devices/pong618/tools/build-release.sh
```

`PONG_BUILD_JOBS`, `PONG_BUILD_MEMORY_MIB`, `PONG_QEMU_DATA` and `PONG_CLANG`
select host resources and emulator/compiler paths. The build creates the kernel,
all selected vendor targets, a frozen module bank, final signatures and diskless
VM tests. Actual selected ELF exports and the kernel symvers must satisfy every
import and CRC. Stale DDK provider metadata cannot satisfy a missing module.

No prebuilt `.ko`, private signing key, phone connection or maintainer working
directory is required as an input. A fresh build generates its own module key.
The `kernel-final`, `signed` and `frozen` output directories are the input to the
companion Pong-kernel importer, with the same reference and exact startup lists.

## Community and LAB profiles

The community reference builds with `CONFIG_PONG_LAB_GUARDS` disabled. Standard
Linux module signature admission remains enforced through
`CONFIG_MODULE_SIG_FORCE=y` and SHA-256. The VM checks trusted modules and rejects
unsigned modules, unknown keys and damaged signatures.

The default wrappers retain the explicit LAB profile and
`docs/build-reference-6.18.60-CLG43.json`. LAB keeps the fixed-UFS write/mount
guards and immutable module payload admission for the development handset.
Its two-pass build freezes the payloads, rebuilds the exact admission list and
re-signs the bank. A trusted signature on a different payload is still rejected
in LAB. Both profiles have positive and negative VM test expectations.

See [Community CLG43](docs/COMMUNITY-CLG43.md) for the profile selection and
packaging requirements. Preserve the generated module key outside Git when
retaining a build. ROM APK/APEX/OTA/AVB signing identities are separate from
the kernel module certificate.

## Runtime and ROM qualification

The development CLG43 kernel combines the DISPCC recovery and PowerStats record
fix with the tested clock handoff and USB reset changes. A separate LAB CLG42
cohort reached CX shutdown and completed an overnight test. Those observations
belong to their recorded image hashes, module parameters and boot IDs.

The community source candidate requires its own complete ROM boot, checkpoint,
data-preserving update and hardware qualification. Host build and VM reports
record those limits. Match the device sources, DTB/DTBO, firmware prerequisites,
module options and init load ordering when packaging a ROM. WLAN calibration
and DRV options must match the qualified companion prebuilt.

The source wrappers perform host builds and tests. Flashing is a separate
operation. Qualify supported RAM/UFS variants and the recovery path before
claiming a general flashable release. Private user data and proprietary
userspace/DSP firmware are not provided by this repository.

Historical source recipes and qualification remain in `docs/rom/`,
`docs/build-evidence/`, `docs/CLG277.md` and
`docs/runtime-regression-6.18.60.md`. These preserve earlier failures and build
inputs. Their older module counts and manifests refer to those checkpoints.

## Authorship and licenses

Upstream ACK/stable history, vendor notices, original licenses and commit
trailers remain intact. Full donor history is retained where imported. Other
package imports retain exact donor mappings in `docs/provenance.json` and
[the provenance guide](docs/provenance-guide.md). Unmapped sources are labelled
explicitly. Source and build adaptation was prepared by Szmazwidi with OpenAI
Codex and Anthropic Claude assistance.

Per-file SPDX and copyright notices take precedence. `LICENSES/` and `COPYING`
retain the kernel license inventory. No private signing keys, personal backups
or private runtime logs belong in these source repositories.
