# DerpFest 17 / Linux 6.18: source-state recipes

`derpfest17-pong618-6.18.60.xml` is the 6.18.60 candidate: it retains the
same pinned projects and updates the device and prebuilt commits. All 1,259
project revisions were checked against the existing local source workspace.
The source-built kernel and module cohort booted on the development phone;
mobile-data and camera NDK tests are documented separately. A full ROM built
from this manifest has not been flashed or qualified, and a clean resync has
not been tested. Some repositories remain private and the prebuilt includes
device-specific LAB protection; this manifest is not a generic flashable release.

`derpfest17-pong618-current.xml` pins **1,259 projects**, including the manually attached
Pong device/vendor/kernel projects that `repo manifest -r` alone omitted.
The current recipe includes the AIDL fingerprint error fix, opt-in sensors
frontend, data-preserving fstab and corrected prebuilt metadata checksums.
`derpfest17-CLS38.xml` preserves the original as-built snapshot;
`next-source-overlay.json` records the two updated project pins.

`patches/series.json` captures the one tracked local platform change required by
the existing build: Nothing compatibility types in the SELinux 202504 ignore
mapping. It does not set SELinux permissive or grant broad new permissions.

The original CLS38 source state produced `DerpFest-v17-20261101-Pong-Community-Stable.zip` on the
maintainer's existing host. **That full ROM has not been flashed/qualified, and
a new full clean resync/build of this packaged recipe has not been performed.**
The 6.18.59 source overlay also completed an incremental full ROM build on the
existing host; that ZIP was not flashed and a clean resync was not tested.
Binary/compatibility checks are recorded under `docs/build-evidence`.
The tested CLS38 kernel/module image set is a separate runtime reference. Do
not equate those results with hardware testing of the complete ROM.

Some required repositories are private. This is a maintainer/access-controlled
recipe until access and redistribution rights are arranged. Repository visibility
is not changed by this document. Vendor firmware/userspace remains proprietary;
retain its original notices and extraction/fixup provenance.

## Reproduce the captured source state

Keep this support checkout outside the Android tree while syncing. Install the
normal AOSP host dependencies, Git LFS and `repo`, and configure GitHub access
without storing credentials in XML or scripts.

```sh
git clone --branch pong-a17-6.18.59 --single-branch \
  https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18.git pong-source
mkdir pong-android && cd pong-android
repo init -u https://github.com/DerpFest-AOSP/manifest.git -b 17
cp ../pong-source/docs/rom/derpfest17-pong618-current.xml .repo/manifests/pong618-current.xml
repo init -m pong618-current.xml
repo sync -j4
repo forall -c 'if git lfs ls-files | grep -q .; then git lfs pull; fi'
python3 ../pong-source/docs/rom/apply-snapshot.py --check
python3 ../pong-source/docs/rom/apply-snapshot.py
source build/envsetup.sh
lunch lineage_Pong-cp2a-userdebug
m derp -j4
```

For the 6.18.60 candidate, use the `pong-a17-6.18.60` modules branch and copy
`derpfest17-pong618-6.18.60.xml` instead. The snapshot patch checks still apply.
The optional camera enumeration recovery and sensors AIDL frontend are disabled
by default; neither is claimed to be device-qualified by this recipe.

The existing device `vendorsetup.sh` reconstructs the split Arcsoft file from
the matching extracted blob parts. An unresolved LFS pointer is not firmware or
a usable library. The patch applier is copied verbatim from the existing Pong
device support and verifies revision and before/after hashes before applying
anything; it neither resets a checkout nor commits someone else's work.

`TARGET_PONG_KERNEL=6.18` uses `device/nothing/Pong-kernel` for Image, DTB, DTBO,
262 signed modules and the 94-module vendor ramdisk subset. The 5.10 common
source checkout remains required for the preserved vendor UAPI headers. The
unused 5.10 external module checkout is deliberately omitted: it is not built
in this configuration, and the maintainer's local commit is not available at
the old remote URL. Do not replace a missing revision with an arbitrary branch.

The old Android 17 manifests in the device README describe an older 5.10
snapshot. Their patch bundle must not be applied to this newer source state.
`source-state.json` records the eight additional project pins and patch origin.

## Before any hardware use

The imported CLS38 prebuilt retains physical write/identity guards specific to
the development phone's UFS geometry and RAM layout. **This is not a generic
flashable community release for every Phone (2) SKU.** Retain critical partition
protection and qualify a target-specific guard policy and data-preserving
recovery procedure before testing another phone. Do not remove protections to
make a source build boot. A ROM flash is not part of this recipe.

Rebuilding the kernel/modules from source additionally requires the two-pass
payload policy and local-key signing process in the main README. Do not combine
a fresh kernel key with the laboratory signed modules. Do not mix stale 5.10
modules in reused `vendor_dlkm` or vendor ramdisk build outputs; the current
module sets must be checked explicitly after any kernel-family switch.

The ROM ZIP target is `derp` in this pinned tree (`vendor/lineage/build/tasks/derp.mk`).
`m nothing` is a configuration check and does not produce the ROM ZIP.
The 5.10 header project includes a pinned KernelSU submodule; `sync-s=true`
in the manifest fetches it for Kconfig/header generation, without enabling
KernelSU in the 6.18 runtime. The 6.18 kernel build is a separate recipe.
