# Nothing Phone (2) — vendor kernel modules for android17-6.18

Kleaf DDK external modules for the Nothing Phone (2) (codename **Pong**, SM8475 "cape", waipio
family) on the Android Common Kernel **android17-6.18** (6.18.32). Nothing's own kernel sources
stop at 5.10.

This branch contains exactly the 159 non-GKI modules that run on the development phone (image
734), the sources they are built from, the device tree of that image and the tools to pack
everything into flashable images. The other 84 modules on the phone are GKI modules built with
the kernel. The kernel is
[android_kernel_nothing_sm8475-6.18](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18),
branch `pong-a17-6.18.32`.

## Status

Tested on one device (slot B, DerpFest 17 userspace, SELinux enforcing):

| Area | State |
|---|---|
| Boot to Android, ADB | works |
| Display, touch (Goodix) | works; needs `vendor.gralloc.disable_ubwc=1` |
| Wi-Fi (QCA6490) | works |
| Bluetooth, BT audio over SLIMbus (btfm) | works; sound card registers |
| Battery / charger driver | works with the stock HAL |
| Audio | playback path runs (HAL counters advance), TFA98xx amplifiers are kept with `no_start=1`, so audible output is not confirmed |
| Camera | **not working**: Titan top GDSC times out |
| Telephony, mobile data | not verified |
| CPU frequency scaling, thermal mitigation | not ported yet |

This is a development tree, not a daily driver.

## Layout

The repository is checked out at `devices/pong618` inside a Kleaf workspace, because the BUILD
files use absolute labels (`//devices/pong618/...`).

* Each top-level directory is a Kleaf package. The number in a name (`scm590`, `ufs-reboot39`,
  ...) is the development stage that finalized that package. The names are kept as they are
  because source paths end up inside the modules, so renaming a package changes the binaries.
* `build/` defines the symbol providers: when a module links against another module, it gets that
  module's `Module.symvers` from the target that builds it in this tree (no prebuilt `.ko`
  anywhere).
* `BUILD.bazel` at the top defines `pong_dist`, which builds and collects the 159 modules.
* `tools/images/` packs kernel and modules into `boot`, `vendor_boot`, `vendor_dlkm` and `dtbo`
  images; `tools/images/pong618/` holds the device data (module lists, init scripts, DT).
* `tools/gen-module-policy.py` regenerates the module allowlist of the kernel's lab guard.
* [docs/MODULES.md](docs/MODULES.md) lists every target and its modules;
  [docs/provenance.json](docs/provenance.json) records where every file comes from (see below);
  [docs/manifest/](docs/manifest/) has the pinned repo manifest.

## Building

Both repositories are private for now: `curl` and `repo sync` need GitHub credentials with
access to them (for example `gh auth setup-git`, or `curl -H "Authorization: token ..."`).

```sh
mkdir pong618 && cd pong618
repo init -u https://android.googlesource.com/kernel/manifest -b common-android17-6.18
# pinned manifest: every AOSP project at the exact commit used for image 734,
# common = the pong-a17-6.18.32 kernel branch, this repository at devices/pong618
curl -L -o .repo/manifests/pong618.xml \
  https://raw.githubusercontent.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18/pong-a17-6.18.32/docs/manifest/pong618-6.18.32.xml
repo init -m pong618.xml
repo sync -c -j8

tools/bazel run //common:kernel_aarch64_dist -- --destdir=out/kernel
tools/bazel run //devices/pong618:pong_dist -- --destdir=out/modules
```

`out/kernel` gets `Image`, the GKI modules, `modules.builtin*` and `Module.symvers`; `out/modules`
gets the 159 vendor modules. The manifest clones the kernel with `clone-depth="1"`; drop that
attribute if you want its history.

Then build the images from the images of the installed ROM (`tools/images/README.md`):

```sh
devices/pong618/tools/images/build_images.py \
    --config devices/pong618/tools/images/pong618/config.json \
    --stock stock --kernel out/kernel/Image --modules out/kernel out/modules \
    --sign bazel-bin/common/kernel_aarch64 \
    --tools prebuilts/kernel-build-tools/linux_musl-x86/bin --out out/images
```

Kleaf leaves the vendor modules unsigned; `--sign` signs every module with the key of the kernel
that was just built (GKI signature protection only lets signed modules use protected symbols).

### Reproducibility

Built from this branch and the pinned manifest, the 159 vendor modules are byte-identical to
the modules running on the development phone (compared without the appended signature). The
kernel `Image` differs from the running one only in the embedded module-signing certificate,
because Kleaf generates a new signing key in every fresh output directory, and the 84 GKI
modules differ only in `.BTF`.

### About the kernel branch

The kernel branch is ACK `android17-6.18` at `926a323dc266` plus five commits marked
`pong: LAB:` and a README. The LAB commits are safety guards of the development phone (writes
allowed only to the userdata/metadata ranges of *that* UFS, no SCSI/BSG passthrough, read-only
protected partitions, kexec disabled, and **only module payloads whose SHA-256 is listed in
`kernel/module/pong251-module-policy.h` may load**). The list holds the modules of image 734.

On any other phone, revert the LAB commits (`README.pong.md` in the kernel branch has the
command) and build as above.

To keep the guards with your own module build, hash the modules, rebuild only the kernel, and
re-sign the modules with the new kernel's key (the signature is not part of the hashed payload):

```sh
tools/bazel run //common:kernel_aarch64_dist -- --destdir=out/kernel
tools/bazel run //devices/pong618:pong_dist -- --destdir=out/modules
devices/pong618/tools/gen-module-policy.py out/kernel out/modules \
    > common/kernel/module/pong251-module-policy.h
tools/bazel run //common:kernel_aarch64_dist -- --destdir=out/kernel-final
devices/pong618/tools/images/build_images.py ... \
    --kernel out/kernel-final/Image --modules out/kernel out/modules \
    --sign bazel-bin/common/kernel_aarch64
```

Use the modules from the first build: Bazel rebuilds the modules after the kernel changes and
their `.BTF` changes with `vmlinux` (`CONFIG_MODULE_ALLOW_BTF_MISMATCH` lets the kernel load the
first ones). This is how the development image was made.

## Authorship and provenance

No code here is presented as written by someone who did not write it:

* This branch starts from the full history of
  [redducc/kernel_nothing_pong-modules](https://github.com/redducc/kernel_nothing_pong-modules)
  (6.6), which itself carries the CodeLinaro/Qualcomm, Nothing, LineageOS and community history.
  Those files are first moved, unchanged, into the package layout.
* Files taken from [redducc/kernel_nothing_pong](https://github.com/redducc/kernel_nothing_pong)
  (6.6 kernel), from the Android Common Kernel and from the Nothing Phone (2) 5.10 kernel are
  merged together with their own history (cut out of those repositories with
  `git fast-export`), so `git log --follow` and `git blame` show their original authors.
* The 6.18 port is in separate per-package commits by Szmazwidi. Most of that code was written
  with OpenAI Codex and part of it with Claude; the commit trailers say so.
* `docs/provenance.json` lists, for every file, its source repository, commit and path, and
  whether it is verbatim, modified for 6.18, or new.
* The device tree blobs in `tools/images/pong618/dt/` are binaries from the development image;
  their README says what is known about their origin.

`git log --follow` across the layout move needs `git config diff.renameLimit 20000`.

## Licenses

Kernel modules are GPL-2.0 (individual files keep their SPDX headers and copyright notices).
Firmware and userspace blobs are not part of this repository.

## Credits

Qualcomm / CodeLinaro, Nothing Technology, the Android Common Kernel and upstream Linux authors,
redducc (6.6 port for the Nothing Phone (2)), AOSPA, LineageOS and everyone in the history of
the files.
