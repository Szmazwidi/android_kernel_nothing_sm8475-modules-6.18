"""Dostawca symboli (Module.symvers) budowany ze źródeł.

ddk_prebuilt_module zachowuje dokładnie tę samą semantykę co dawne gotowe pliki .ko
(do modułu zależnego trafia tylko Module.symvers dostawcy, bez jego nagłówków i konfiguracji),
ale plik .ko i Module.symvers pochodzą z celu budowanego w tym drzewie.
"""

load("//build/kernel/kleaf:kernel.bzl", "ddk_prebuilt_module")

def _pick_impl(ctx):
    files = [f for f in ctx.attr.module[DefaultInfo].files.to_list() if f.basename == ctx.attr.basename]
    if len(files) != 1:
        fail("{}: oczekiwano jednego pliku {} w {}, jest {}".format(ctx.label, ctx.attr.basename, ctx.attr.module.label, files))
    return [DefaultInfo(files = depset(files))]

_pick = rule(
    implementation = _pick_impl,
    attrs = {
        "module": attr.label(mandatory = True),
        "basename": attr.string(mandatory = True),
    },
)

def pong_provider(name, module, ko, kernel_build = "//common:kernel_aarch64"):
    """Opakowuje cel ddk_module jako ddk_prebuilt_module (tylko symbole)."""
    target_name = module.split(":")[-1]
    _pick(name = name + "_ko", module = module, basename = ko)
    _pick(name = name + "_symvers", module = module, basename = target_name + "_Module.symvers")
    native.filegroup(name = name + "_order", srcs = [module], output_group = "modules.order")
    ddk_prebuilt_module(
        name = name,
        src = ":" + name + "_ko",
        module_symvers = ":" + name + "_symvers",
        modules_order = ":" + name + "_order",
        kernel_build = kernel_build,
    )
