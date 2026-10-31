# Build-rule adaptation: original sources retained; binary symbol inputs resolved from source.
load('//build/kernel/kleaf:kernel.bzl', 'ddk_module')

def define_mem(target, variant):
    kernel_build_variant = '{}_{}'.format(target, variant)
    include_base = '../../../{}'.format(native.package_name())
    ddk_module(name='{}_rmnet_mem'.format(kernel_build_variant), out='rmnet_mem.ko', includes=['.'], hdrs=['rmnet_mem.h'], srcs=['rmnet_mem_main.c', 'rmnet_mem_nl.c', 'rmnet_mem_nl.h', 'rmnet_mem_pool.c', 'rmnet_mem_priv.h'], kernel_build='//common:kernel_aarch64', deps=['//devices/pong618/wlan452/providers:vendor_headers'], copts=['-Wno-misleading-indentation', '-ferror-limit=0'])
