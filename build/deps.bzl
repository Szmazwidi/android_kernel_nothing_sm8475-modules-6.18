"""Symbol providers for packages whose modules are defined by .bzl macros.

Computed from the symbols the running modules actually import (this replaces the
former lists of prebuilt .ko files)."""

PONG_PACKAGE_DEPS = {
    "devices/pong618/av580/qcom/opensource/audio-kernel": [
        "//devices/pong618/build:prov_av580_qcom_opensource_mm_drivers_msm_ext_display_waipio_perf_msm_ext_display",
        "//devices/pong618/build:prov_platform_pinctrl",
        "//devices/pong618/build:prov_scm590_scm590",
        "//devices/pong618/build:prov_subsystems407_remote407",
    ],
    "devices/pong618/av580/qcom/opensource/camera-kernel": [
        "//devices/pong618/build:prov_av580_qcom_opensource_synx_kernel_waipio_perf_modules",
        "//devices/pong618/build:prov_display76_llcc76",
        "//devices/pong618/build:prov_graphics68_mdt",
        "//devices/pong618/build:prov_membuf158_mem_buf_dev",
        "//devices/pong618/build:prov_memory_util",
        "//devices/pong618/build:prov_platform_socinfo",
        "//devices/pong618/build:prov_scm590_scm590",
    ],
    "devices/pong618/av580/qcom/opensource/dsp-kernel": [
        "//devices/pong618/build:prov_membuf158_mem_buf_dev",
        "//devices/pong618/build:prov_scm590_scm590",
        "//devices/pong618/build:prov_subsystems407_remote407",
    ],
    "devices/pong618/wlan452/qcacld-3.0": [
        "//devices/pong618/build:prov_cfg468_cfg80211",
        "//devices/pong618/build:prov_subsystems407_wireless407",
        "//devices/pong618/build:prov_wlan452_platform_helpers",
    ],
}

def pong_package_deps():
    """Symbol providers for the calling package."""
    return list(PONG_PACKAGE_DEPS.get(native.package_name(), []))
