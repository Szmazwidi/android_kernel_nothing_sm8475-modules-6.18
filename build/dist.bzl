"""Collects selected .ko files from ddk_module targets."""

def _pong_modules_impl(ctx):
    out = {}
    for target, names in ctx.attr.modules.items():
        files = {}
        for f in target[DefaultInfo].files.to_list():
            if f.extension == "ko":
                files[f.basename] = f
        for name in names.split(" "):
            if name not in files:
                fail("{} does not build {}".format(target.label, name))
            if name in out:
                fail("{} is built by more than one target".format(name))
            out[name] = files[name]
    return [DefaultInfo(files = depset([out[n] for n in sorted(out)]))]

pong_modules = rule(
    implementation = _pong_modules_impl,
    doc = "The listed .ko files of each ddk_module target (keys: targets, values: space-separated .ko names).",
    attrs = {"modules": attr.label_keyed_string_dict(mandatory = True)},
)
