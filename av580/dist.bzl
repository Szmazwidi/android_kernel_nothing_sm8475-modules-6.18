load("@rules_pkg//pkg:install.bzl", "pkg_install")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
def copy_to_dist_dir(name, data, **kwargs):
    pkg_files(name=name+"_files", srcs=data, strip_prefix=strip_prefix.files_only())
    pkg_install(name=name, srcs=[":"+name+"_files"])
