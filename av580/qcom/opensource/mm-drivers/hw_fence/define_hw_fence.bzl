load("//build/kernel/kleaf:kernel.bzl", "ddk_module", "ddk_submodule")
load("//devices/pong618/av580:dist.bzl", "copy_to_dist_dir")
load("//devices/pong618/av580:variants.bzl", "get_all_variants")

def _define_module(target, variant):
    tv = "{}_{}".format(target, variant)
    ddk_module(
        name = "{}_msm_hw_fence".format(tv),
        srcs = [
            "src/hw_fence_drv_debug.c",
            "src/hw_fence_drv_ipc.c",
            "src/hw_fence_drv_priv.c",
            "src/hw_fence_drv_utils.c",
            "src/msm_hw_fence.c",
            "src/msm_hw_fence_synx_translation.c",
        ],
        out = "msm_hw_fence.ko",
        defconfig = "defconfig",
        kconfig = "Kconfig",
        conditional_srcs = {
            "CONFIG_DEBUG_FS": {
                True: ["src/hw_fence_ioctl.c"],
            },
        },
        deps = [
            "//devices/pong618/av580/providers:vendor_headers",
            "//devices/pong618/av580/qcom/opensource/synx-kernel:synx_headers",
            "//devices/pong618/av580/qcom/opensource/mm-drivers:mm_drivers_headers",
        ],
        kernel_build = "//common:kernel_aarch64",
    )

    copy_to_dist_dir(
        name = "{}_msm_hw_fence_dist".format(tv),
        data = [":{}_msm_hw_fence".format(tv)],
        dist_dir = "out/target/product/{}/dlkm/lib/modules".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )

def define_hw_fence():
    for (t, v) in get_all_variants():
        if t == "parrot":
            continue
        else:
            _define_module(t, v)
