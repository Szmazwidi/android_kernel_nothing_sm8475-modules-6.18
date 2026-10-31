> Historical image-734 recipe (6.18.32 / 243 modules). The current 6.18.59 source set has 262 reference modules; this recipe has not been promoted to a tested CLS33 image profile. See the repository README.

# Image tools

`build_images.py` turns a kernel and module build into flashable `boot`, `vendor_boot`,
`vendor_dlkm` and `dtbo` images, on top of the images of the ROM that is installed on the
phone. Device data lives in `pong618/`:

| File | Purpose |
|---|---|
| `pong618/config.json` | kernel command line, DTB/DTBO, first-stage module list and order (`vendor_boot`), the modules in `vendor_dlkm`, module options |
| `pong618/pong618.rc` | top-level init script, selected with `androidboot.init_rc=` on the kernel command line; imports what init imports by default, plus the file below |
| `pong618/pong618-modules.rc` | late module loading: DSP, battery, Bluetooth and Wi-Fi automatically; audio, Bluetooth audio and camera on request (`setprop debug.pong618.load_audiofoundation 1`, `..._btaudio`, `..._audio`, `..._camera`); a UFS power-saving workaround |
| `pong618/dt/` | DTBs and DTBO of the working image, see its README |

`erofs_labels.py` reads the SELinux label of every path in an EROFS image; the rebuilt
`vendor_dlkm` gets exactly the labels of the stock one.

## Usage

```sh
# from the Kleaf workspace, after building the kernel and the modules (see the top README)
mkdir stock   # boot.img vendor_boot.img vendor_dlkm.img dtbo.img of the installed ROM
devices/pong618/tools/images/build_images.py \
    --config devices/pong618/tools/images/pong618/config.json \
    --stock stock --kernel out/kernel/Image --modules out/kernel out/modules \
    --sign bazel-bin/common/kernel_aarch64 \
    --tools prebuilts/kernel-build-tools/linux_musl-x86/bin --out out/images
```

`--sign` (re-)signs every module with the key of the kernel build in that directory
(`certs/signing_key.pem`, `certs/signing_key.x509`, `scripts/sign-file`). Kleaf does not sign
the vendor modules, and a kernel rebuilt after the modules has a new key. The signed payload
stays byte-identical, so the kernel's module allowlist still matches.

Host requirements: Python 3, `lz4`, `depmod` (kmod). The other tools come from the
workspace (`--tools`).

What is taken from the stock images: boot ramdisk and header fields, vendor ramdisk
(everything except `lib/modules`), bootconfig and vendor command line, `vendor_dlkm`
`etc/` files, SELinux labels, partition sizes, and the AVB footer properties and salts.
The AVB footers use algorithm `NONE`, as on the stock images; flashing these images
needs a `vbmeta` with verification disabled, like the one on the development phone.

## What it was checked against

Run with the stock images of the development phone, the kernel `Image` and the 243 modules of
image 734, the output has the same kernel, the same DTB and the same 243 module files as the
image 734 (`vendor_boot` ramdisk and `vendor_dlkm`). It differs from image 734 in what was
lab-only there: diagnostic probe binaries and their init services, the `androidboot.pong618_lab`
marker and the properties that gated module loading on it. It also keeps the stock
`vendor_dlkm/etc` files (`build.prop`, `fs_config_*`), which image 734 had dropped.
