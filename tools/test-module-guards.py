#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Boot a diskless QEMU test of default signature enforcement and payload gates.

Uses only libarc4, never the phone. Test keys stay in the new private output dir.
No module.sig_enforce boot argument is supplied: enforcement must be the default.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
MAGIC = b'~Module signature appended~\n'


def entry(stream, name, mode, data=b'', rmajor=0, rminor=0):
    encoded = name.encode()+b'\0'
    fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, rmajor, rminor, len(encoded), 0]
    stream.write(('070701'+''.join('%08x' % value for value in fields)).encode())
    stream.write(encoded)
    stream.write(b'\0'*(-stream.tell() % 4))
    stream.write(data)
    stream.write(b'\0'*(-stream.tell() % 4))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['qemu', 'kernel', 'signed', 'frozen', 'key', 'cert', 'output']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--sign-file', type=Path, help='Legacy argument, unused with OpenSSL CMS')
    parser.add_argument('--qemu-data', type=Path)
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    os.umask(0o077)
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    out = args.output
    def run(command):
        p = subprocess.run(command, capture_output=True)
        if p.returncode:
            raise RuntimeError(p.stderr.decode(errors='replace')[-1500:])
    source = args.frozen/'modules/libarc4.ko'
    (out/'unsigned.ko').write_bytes(source.read_bytes())
    shutil.copyfile(args.signed/'libarc4.ko', out/'good.ko')
    # A different key signs the exact admitted payload: only signature trust
    # should reject it. Keep the disposable key private, outside source control.
    run(['openssl', 'req', '-new', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
         '-subj', '/CN=Pong test-only untrusted key/', '-keyout', str(out/'test-only.key'),
         '-out', str(out/'test-only.pem')])
    shutil.copyfile(source, out/'wrong-key.ko')
    spec = importlib.util.spec_from_file_location('prepare', HERE/'prepare-modules.py')
    prepare = importlib.util.module_from_spec(spec); spec.loader.exec_module(prepare)
    (out/'wrong-key.ko').write_bytes(prepare.sign_payload(source.read_bytes(), out/'test-only.key', out/'test-only.pem'))
    bad = bytearray((out/'good.ko').read_bytes())
    bad[-len(MAGIC)-12-1] ^= 1  # RSA signature byte, not ASN.1/trailer structure.
    (out/'corrupt-signature.ko').write_bytes(bad)
    changed = bytearray(source.read_bytes()); changed[-1] ^= 1
    (out/'changed-signed.ko').write_bytes(changed)
    inform = 'PEM' if args.cert.read_bytes().startswith(b'-----BEGIN') else 'DER'
    pem = subprocess.check_output(['openssl', 'x509', '-inform', inform, '-in', str(args.cert), '-outform', 'PEM'])
    (out/'final-certificate.pem').write_bytes(pem)
    (out/'changed-signed.ko').write_bytes(prepare.sign_payload(bytes(changed), args.key, out/'final-certificate.pem'))
    run([args.clang, '--target=aarch64-linux-gnu', '-nostdlib', '-static', '-fuse-ld=lld',
         '-O2', '-fno-builtin', '-fno-stack-protector', '-Wl,-e,_start',
         str(HERE/'tests/module-guard-smoke.c'), '-o', str(out/'init')])
    archive = io.BytesIO()
    for name in ['dev', 'sys', 'proc']:
        entry(archive, name, 0o040755)
    entry(archive, 'dev/console', 0o020600, rmajor=5, rminor=1)
    entry(archive, 'dev/kmsg', 0o020600, rmajor=1, rminor=11)
    entry(archive, 'init', 0o100755, (out/'init').read_bytes())
    for file in sorted(out.glob('*.ko')):
        entry(archive, file.name, 0o100644, file.read_bytes())
    entry(archive, 'TRAILER!!!', 0)
    (out/'test.cpio').write_bytes(archive.getvalue())
    log = out/'serial.log'
    command = [str(args.qemu)]
    if args.qemu_data: command += ['-L', str(args.qemu_data)]
    command += ['-no-user-config', '-nodefaults', '-machine', 'virt,gic-version=3,virtualization=off',
                '-accel', 'tcg,thread=multi', '-cpu', 'max', '-smp', '2', '-m', '1536',
                '-display', 'none', '-serial', 'file:'+str(log), '-monitor', 'none', '-nic', 'none',
                '-no-reboot', '-kernel', str(args.kernel), '-initrd', str(out/'test.cpio'),
                '-append', 'rdinit=/init panic=0 loglevel=7 console=ttyAMA0,115200 nokaslr']
    start = time.monotonic()
    with (out/'qemu.stderr').open('wb') as error:
        proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=error)
        while proc.poll() is None and time.monotonic()-start < 180:
            if log.exists() and 'PONG_GUARD_DONE' in log.read_text(errors='replace'):
                break
            time.sleep(1)
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    text = log.read_text(errors='replace') if log.exists() else ''
    results = {name: int(value) for name, value in re.findall(r'PONG_GUARD_MODULE /(\S+) (-?\d+)', text)}
    expected = {'unsigned.ko': -129, 'wrong-key.ko': -129, 'corrupt-signature.ko': -129,
                'changed-signed.ko': -1, 'good.ko': 0}
    passed = results == expected and 'PONG_GUARD_ENFORCE Y' in text and 'PONG_GUARD_DONE' in text
    result = dict(status='PASS' if passed else 'FAIL', results=results, expected=expected,
                  forced_by_default='PONG_GUARD_ENFORCE Y' in text,
                  kernel_sha256=hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
                  seconds=round(time.monotonic()-start, 2), phone_operations=0,
                  disks_attached=0, hardware_tested=False)
    (out/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
