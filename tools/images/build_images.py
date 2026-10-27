#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build boot, vendor_boot, vendor_dlkm and dtbo images for the 6.18 kernel.

The images of the ROM installed on the phone ("stock") provide everything that is not
kernel-specific: boot ramdisk, vendor ramdisk, bootconfig, vendor_dlkm etc/ files,
SELinux labels, partition sizes, AVB properties and salts. The kernel, the modules,
their metadata, the device tree and the init scripts come from this repository:

  boot.img         stock header and ramdisk, new kernel, cmdline from the config
  vendor_boot.img  DTB from the config (or --dtb, else the stock one), stock bootconfig
                   and vendor cmdline; the vendor ramdisk keeps everything except
                   lib/modules, which gets the first-stage modules under
                   lib/modules/<kernel release>/
  vendor_dlkm.img  stock etc/ files, the modules of the config in lib/modules, the init
                   scripts from the config, EROFS (lz4hc) with the stock labels and an
                   AVB hashtree footer
  dtbo.img         the dtbo from the config (or --dtbo) with the stock AVB footer data

Tools: unpack_bootimg, mkbootimg, avbtool, mkfs.erofs, fsck.erofs and fec (all in
prebuilts/kernel-build-tools/linux_musl-x86/bin of the Kleaf workspace, pass that with
--tools), plus lz4 and depmod (kmod) from the host.

Example:
  tools/images/build_images.py --config tools/images/pong618/config.json \\
      --stock stock/ --kernel out/kernel/Image --modules out/kernel out/modules \\
      --tools prebuilts/kernel-build-tools/linux_musl-x86/bin --out out/images
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import erofs_labels  # noqa: E402

FIXED_TIME = 1230768000  # same timestamp AOSP uses for reproducible images


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, **kw)
    if r.returncode:
        sys.exit('failed: %s\n%s%s' % (' '.join(map(str, cmd)), r.stdout.decode(errors='replace'),
                                       r.stderr.decode(errors='replace')))
    return r.stdout


# ---------------------------------------------------------------- cpio (newc)

def cpio_read(data):
    entries, pos = [], 0
    while True:
        if data[pos:pos + 6] != b'070701':
            raise ValueError('not a newc cpio archive at %d' % pos)
        f = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
        mode, size, namesize = f[1], f[6], f[11]
        name = data[pos + 110:pos + 110 + namesize - 1].decode()
        pos = (pos + 110 + namesize + 3) & ~3
        body = data[pos:pos + size]
        pos = (pos + size + 3) & ~3
        if name == 'TRAILER!!!':
            return entries
        entries.append((name, mode, body))


def cpio_write(entries):
    out = bytearray()

    def add(ino, name, mode, body):
        nb = name.encode() + b'\0'
        hdr = '070701' + ''.join('%08X' % v for v in (
            ino, mode, 0, 0, 2 if mode & 0o170000 == 0o040000 else 1, 0, len(body),
            0, 0, 0, 0, len(nb), 0))
        out.extend(hdr.encode() + nb)
        out.extend(b'\0' * (-len(out) % 4))
        out.extend(body)
        out.extend(b'\0' * (-len(out) % 4))

    for i, (name, mode, body) in enumerate(sorted(entries, key=lambda e: e[0])):
        add(i + 1, name, mode, body)
    add(0, 'TRAILER!!!', 0, b'')
    return bytes(out)


def decompress(data):
    if data[:4] == b'\x02\x21\x4c\x18':
        return run(['lz4', '-dc'], input=data), 'lz4'
    if data[:2] == b'\x1f\x8b':
        return run(['gzip', '-dc'], input=data), 'gzip'
    return data, None


def compress(data, kind):
    if kind == 'lz4':
        return run(['lz4', '-c', '-l', '-12', '--favor-decSpeed'], input=data)
    if kind == 'gzip':
        return run(['gzip', '-9n'], input=data)
    return data


# ---------------------------------------------------------------- modules

def find_modules(dirs):
    mods, extra = {}, {}
    for d in dirs:
        for dp, _, fn in os.walk(d):
            for f in fn:
                p = os.path.join(dp, f)
                if f.endswith('.ko'):
                    if f in mods and open(mods[f], 'rb').read() != open(p, 'rb').read():
                        sys.exit('two different %s: %s and %s' % (f, mods[f], p))
                    mods[f] = p
                elif f in ('modules.builtin', 'modules.builtin.modinfo'):
                    extra[f] = p
    return mods, extra


SIG_MAGIC = b'~Module signature appended~\n'


def strip_signature(data):
    if not data.endswith(SIG_MAGIC):
        return data
    body = data[:-len(SIG_MAGIC)]
    sig_len = int.from_bytes(body[-4:], 'big')
    return body[:-12 - sig_len]


def sign_modules(mods, kdir, tmp):
    """Re-sign every module with the key of the kernel build in kdir
    (bazel-bin/common/kernel_aarch64: certs/signing_key.pem, certs/signing_key.x509,
    scripts/sign-file). The signed payload stays byte-identical."""
    key = os.path.join(kdir, 'certs', 'signing_key.pem')
    cert = os.path.join(kdir, 'certs', 'signing_key.x509')
    tool = os.path.join(kdir, 'scripts', 'sign-file')
    for p in (key, cert, tool):
        if not os.path.exists(p):
            sys.exit('missing %s' % p)
    algo = 'sha1'
    cfg = os.path.join(kdir, '.config')
    if os.path.exists(cfg):
        m = re.search(r'^CONFIG_MODULE_SIG_HASH="(\w+)"', open(cfg).read(), re.M)
        algo = m.group(1) if m else algo
    # Kleaf builds sign-file against the musl prebuilts of the workspace; when its rpath does
    # not resolve (e.g. outside the execroot), run it through the musl loader of --tools.
    cmd = [tool]
    probe = subprocess.run([tool], capture_output=True)
    if b'unable to find loader' in probe.stderr:
        lib = os.path.join(os.path.dirname(shutil.which('mkbootimg')), '..', 'lib64')
        loader = os.path.join(lib, 'libc_musl.so')
        if not os.path.exists(loader):
            sys.exit('%s cannot find its libraries; pass --tools' % tool)
        os.environ['LD_LIBRARY_PATH'] = os.path.abspath(lib)
        cmd = [loader, tool]
    out = os.path.join(tmp, 'signed')
    os.makedirs(out)
    for name, path in list(mods.items()):
        dst = os.path.join(out, name)
        open(dst, 'wb').write(strip_signature(open(path, 'rb').read()))
        run(cmd + [algo, key, cert, dst])
        mods[name] = dst


def kernel_release(ko):
    m = re.search(rb'vermagic=(\S+)', open(ko, 'rb').read())
    if not m:
        sys.exit('no vermagic in %s' % ko)
    return m.group(1).decode()


def resolve(names, mods):
    out = []
    for n in names:
        f = n if n.endswith('.ko') else n + '.ko'
        if f not in mods:
            sys.exit('module %s from the config was not built' % f)
        out.append(f)
    return out


def module_dir(dst, files, mods, extra, kver, load, options, blocklist):
    """Copy modules into dst and generate the depmod files (paths relative to dst)."""
    with tempfile.TemporaryDirectory() as root:
        kd = os.path.join(root, 'lib/modules', kver)
        os.makedirs(kd)
        for f in files:
            shutil.copyfile(mods[f], os.path.join(kd, f))
        for f, p in extra.items():
            shutil.copyfile(p, os.path.join(kd, f))
        run(['depmod', '-b', root, kver])
        os.makedirs(dst, exist_ok=True)
        for f in sorted(os.listdir(kd)):
            if not f.endswith('.bin'):  # Android's libmodprobe reads only the text files
                shutil.copyfile(os.path.join(kd, f), os.path.join(dst, f))
    for name, lines in (('modules.load', load), ('modules.load.recovery', load),
                        ('modules.order', load), ('modules.options', options),
                        ('modules.blocklist', blocklist)):
        with open(os.path.join(dst, name), 'w') as fh:
            fh.write(''.join(l + '\n' for l in lines))


# ---------------------------------------------------------------- images

def unpack(img, out):
    args = run(['unpack_bootimg', '--boot_img', img, '--out', out, '--format=mkbootimg']).decode()
    return shlex.split(args)


def set_arg(args, key, value):
    if key in args:
        args[args.index(key) + 1] = value
    else:
        args += [key, value]


def avb_info(img):
    r = subprocess.run(['avbtool', 'info_image', '--image', img], capture_output=True)
    if r.returncode:
        return None
    text = r.stdout.decode()
    salt = re.search(r'Salt:\s+([0-9a-f]+)', text)
    props = re.findall(r"Prop: (\S+) -> '(.*)'", text)
    return dict(salt=salt.group(1) if salt else None, props=props)


def avb_footer(img, stock, name, hashtree=False):
    info = avb_info(stock)
    if info is None:
        print('  %s: the stock image has no AVB footer, none added' % name)
        return
    cmd = ['avbtool', 'add_hashtree_footer' if hashtree else 'add_hash_footer',
           '--image', img, '--partition_name', name,
           '--partition_size', str(os.path.getsize(stock)), '--algorithm', 'NONE']
    if info['salt']:
        cmd += ['--salt', info['salt']]
    if hashtree:
        cmd += ['--hash_algorithm', 'sha256']
    for k, v in info['props']:
        cmd += ['--prop', '%s:%s' % (k, v)]
    run(cmd)


def build_boot(a, cfg, tmp):
    stock = os.path.join(a.stock, 'boot.img')
    args = unpack(stock, os.path.join(tmp, 'boot'))
    set_arg(args, '--kernel', a.kernel)
    set_arg(args, '--cmdline', cfg.get('kernel_cmdline', ''))
    out = os.path.join(a.out, 'boot.img')
    run(['mkbootimg'] + args + ['--output', out])
    avb_footer(out, stock, 'boot')
    return out


def build_vendor_boot(a, cfg, mods, extra, kver, tmp):
    stock = os.path.join(a.stock, 'vendor_boot.img')
    d = os.path.join(tmp, 'vendor_boot')
    args = unpack(stock, d)
    frags = [args[i + 1] for i, x in enumerate(args) if x == '--vendor_ramdisk_fragment']
    if len(frags) != 1:
        sys.exit('expected one vendor ramdisk fragment, found %d' % len(frags))
    raw, kind = decompress(open(frags[0], 'rb').read())
    entries = [e for e in cpio_read(raw) if not (e[0] == 'lib/modules' or e[0].startswith('lib/modules/'))]
    first = resolve(cfg['first_stage'], mods)
    md = os.path.join(tmp, 'first_stage')
    module_dir(md, first, mods, extra, kver, first, cfg.get('first_stage_options', []), cfg.get('blocklist', []))
    names = {e[0] for e in entries}
    for p in ('lib', 'lib/modules', 'lib/modules/' + kver):
        if p not in names:
            entries.append((p, 0o040755, b''))
    for f in sorted(os.listdir(md)):
        entries.append(('lib/modules/%s/%s' % (kver, f), 0o100644, open(os.path.join(md, f), 'rb').read()))
    ramdisk = os.path.join(tmp, 'vendor_ramdisk')
    open(ramdisk, 'wb').write(compress(cpio_write(entries), kind))
    args[args.index(frags[0])] = ramdisk
    dtb = a.dtb or cfg_path(a, cfg, 'dtb')
    if dtb:
        set_arg(args, '--dtb', dtb)
    out = os.path.join(a.out, 'vendor_boot.img')
    run(['mkbootimg'] + args + ['--vendor_boot', out])
    avb_footer(out, stock, 'vendor_boot')
    return out


def cfg_path(a, cfg, key):
    v = cfg.get(key)
    return os.path.join(os.path.dirname(os.path.abspath(a.config)), v) if v else None


def build_dtbo(a, cfg):
    src = a.dtbo or cfg_path(a, cfg, 'dtbo')
    if not src:
        return None
    out = os.path.join(a.out, 'dtbo.img')
    shutil.copyfile(src, out)
    stock = os.path.join(a.stock, 'dtbo.img')
    if os.path.exists(stock):
        avb_footer(out, stock, 'dtbo')
    return out


def build_vendor_dlkm(a, cfg, mods, extra, kver, tmp):
    stock = os.path.join(a.stock, 'vendor_dlkm.img')
    tree = os.path.join(tmp, 'vendor_dlkm')
    run(['fsck.erofs', '--extract=' + tree, stock])
    stock_labels = erofs_labels.labels(stock)
    shutil.rmtree(os.path.join(tree, 'lib', 'modules'))
    every = resolve(cfg['vendor_dlkm_modules'], mods) if 'vendor_dlkm_modules' in cfg else sorted(mods)
    module_dir(os.path.join(tree, 'lib', 'modules'), every, mods, extra, kver,
               resolve(cfg.get('vendor_dlkm_load', []), mods), cfg.get('vendor_dlkm_options', []), [])
    cfgdir = os.path.dirname(os.path.abspath(a.config))
    for rc in cfg.get('init_rc', []):
        dst = os.path.join(tree, 'etc', 'init')
        os.makedirs(dst, exist_ok=True)
        shutil.copyfile(os.path.join(cfgdir, rc), os.path.join(dst, os.path.basename(rc)))
    # labels: stock label for existing paths, otherwise the label of the closest parent
    lines = []
    for dp, dn, fn in os.walk(tree):
        for n in [''] + dn + fn:
            rel = os.path.relpath(os.path.join(dp, n), tree)
            p = '/' if rel == '.' else '/' + rel
            if n == '' and p != '/':
                continue
            q = p
            while q not in stock_labels or not stock_labels[q][0]:
                q = os.path.dirname(q)
            lines.append('%s %s' % (re.escape('/vendor_dlkm' + ('' if p == '/' else p)), stock_labels[q][0]))
    for dp, dn, fn in os.walk(tree):
        os.chmod(dp, 0o755)
        for f in fn:
            os.chmod(os.path.join(dp, f), 0o644)
    fc = os.path.join(tmp, 'vendor_dlkm.fc')
    open(fc, 'w').write('\n'.join(sorted(set(lines))) + '\n')
    out = os.path.join(a.out, 'vendor_dlkm.img')
    uuid = str(__import__('uuid').UUID(hashlib.sha256(kver.encode()).hexdigest()[:32]))
    run(['mkfs.erofs', '-z', 'lz4hc', '-T', str(FIXED_TIME), '-U', uuid, '--all-root',
         '--mount-point=vendor_dlkm', '--file-contexts=' + fc, out, tree])
    avb_footer(out, stock, 'vendor_dlkm', hashtree=True)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--config', required=True, help='device config (tools/images/<device>/config.json)')
    p.add_argument('--stock', required=True, help='directory with the ROM boot.img, vendor_boot.img, vendor_dlkm.img')
    p.add_argument('--kernel', required=True, help='kernel Image')
    p.add_argument('--modules', required=True, nargs='+', help='directories searched for .ko files')
    p.add_argument('--dtb', help='DTB for vendor_boot (default: "dtb" of the config, else the stock one)')
    p.add_argument('--dtbo', help='dtbo image (default: "dtbo" of the config, else none is written)')
    p.add_argument('--sign', metavar='KERNEL_BIN_DIR',
                   help='re-sign the modules with the key of this kernel build '
                        '(bazel-bin/common/kernel_aarch64); needed when the kernel was rebuilt after the modules')
    p.add_argument('--tools', help='directory prepended to PATH (Kleaf prebuilts/kernel-build-tools/linux_musl-x86/bin)')
    p.add_argument('--out', required=True)
    p.add_argument('--only', nargs='+', choices=['boot', 'vendor_boot', 'vendor_dlkm', 'dtbo'])
    a = p.parse_args()
    if a.tools:
        os.environ['PATH'] = os.path.abspath(a.tools) + os.pathsep + os.environ['PATH']
    for t in ('unpack_bootimg', 'mkbootimg', 'avbtool', 'mkfs.erofs', 'fsck.erofs', 'lz4', 'depmod'):
        if not shutil.which(t):
            sys.exit('%s not found (see --tools)' % t)
    a.kernel = os.path.abspath(a.kernel)
    cfg = json.load(open(a.config))
    mods, extra = find_modules(a.modules)
    if not mods:
        sys.exit('no modules found')
    kver = kernel_release(next(iter(mods.values())))
    os.makedirs(a.out, exist_ok=True)
    print('kernel release %s, %d modules' % (kver, len(mods)))
    with tempfile.TemporaryDirectory() as tmp:
        if a.sign:
            sign_modules(mods, os.path.abspath(a.sign), tmp)
            print('re-signed %d modules with %s' % (len(mods), a.sign))
        todo = a.only or ['boot', 'vendor_boot', 'vendor_dlkm', 'dtbo']
        if 'boot' in todo:
            print('boot:', build_boot(a, cfg, tmp))
        if 'vendor_boot' in todo:
            print('vendor_boot:', build_vendor_boot(a, cfg, mods, extra, kver, tmp))
        if 'vendor_dlkm' in todo:
            print('vendor_dlkm:', build_vendor_dlkm(a, cfg, mods, extra, kver, tmp))
        if 'dtbo' in todo:
            print('dtbo:', build_dtbo(a, cfg))


if __name__ == '__main__':
    main()
