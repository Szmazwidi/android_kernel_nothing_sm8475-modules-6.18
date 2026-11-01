#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Freeze exactly the selected 262 modules, then sign with the final kernel key.

No phone access, image flashing, policy relaxation or private-key publication.
The final kernel must embed both the signing certificate and every frozen
payload entry before the signing phase will proceed.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('pong_module_policy', HERE/'gen-module-policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sign_payload(body, key, certificate_pem):
    """Detached SHA-256 CMS, matching the kernel PKCS#7 module trailer.

    Use full host OpenSSL CMS for an independently checked final signing path;
    native kernel signature verification and the exact payload gate stay on.
    """
    signature = subprocess.run(['openssl', 'cms', '-sign', '-binary', '-noattr', '-nocerts',
                                '-nosmimecap', '-md', 'sha256', '-outform', 'DER',
                                '-signer', str(certificate_pem), '-inkey', str(key)],
                               input=body, capture_output=True, check=True).stdout
    return body+signature+policy.SIG_STRUCT.pack(0, 0, 2, 0, 0, len(signature))+policy.MAGIC


def freeze(args):
    reference = json.loads(args.reference.read_text())
    with tempfile.TemporaryDirectory() as temp:
        report = Path(temp)/'build-check.json'
        subprocess.run([sys.executable, str(HERE/'verify_build.py'), '--kernel', str(args.kernel),
                        '--modules', str(args.modules), '--reference', str(args.reference),
                        '--report', str(report)], check=True)
        check = json.loads(report.read_text())
    sources = {**{n: args.kernel/n for n in reference['gki']},
               **{n: args.modules/n for n in reference['vendor']}}
    rows, bodies = {}, {}
    for name, source in sorted(sources.items()):
        raw = source.read_bytes()
        body = policy.payload(raw)
        if body[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Invalid module: '+name)
        rows[name] = dict(payload_sha256=sha(body), payload_bytes=len(body), source_sha256=sha(raw))
        bodies[name] = body
    manifest = args.output/'manifest.json'
    if manifest.exists():
        old = json.loads(manifest.read_text())
        if old['modules'] != rows or any(sha((args.output/'modules'/n).read_bytes()) != r['payload_sha256']
                                         for n, r in rows.items()):
            raise ValueError('Frozen output differs; choose a new --output, do not overwrite a signed set')
        print('Already frozen and verified:', args.output)
        return
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('Output is not empty; choose a new --output')
    (args.output/'modules').mkdir(parents=True, exist_ok=True)
    for name, body in bodies.items():
        (args.output/'modules'/name).write_bytes(body)
    generated = subprocess.check_output([sys.executable, str(HERE/'gen-module-policy.py'),
                                         str(args.output/'modules')])
    (args.output/'pong251-module-policy.h').write_bytes(generated)
    (args.output/'build-report.json').write_text(json.dumps(check, indent=2)+'\n')
    manifest.write_text(json.dumps(dict(schema='pong-frozen-modules-v1', modules=rows,
                                       policy_sha256=sha(generated), kernel_firstpass_sha256=check['kernel_sha256']),
                                   indent=2)+'\n')
    print('Frozen', len(rows), 'selected modules:', args.output)


def sign(args):
    manifest = json.loads((args.frozen/'manifest.json').read_text())
    initial = json.loads((args.frozen/'build-report.json').read_text())
    assert set(manifest['modules']) == {r['name'] for r in initial['modules']}, 'Incomplete frozen manifest'
    final = args.image.parent
    config = final/'kernel_aarch64_dot_config'
    assert config.is_file(), 'Use Image from the final Kleaf dist, with config and symvers alongside it'
    assert 'CONFIG_MODULE_SIG_FORCE=y' in config.read_text(), 'Final kernel must enforce signatures'
    assert 'CONFIG_MODULE_SIG_SHA256=y' in config.read_text(), 'Final kernel must select SHA256 signing'
    symvers = next((final/n for n in ['Module.symvers', 'kernel_aarch64_Module.symvers'] if (final/n).is_file()), None)
    assert symvers, 'Final kernel symvers missing'
    # Recheck the frozen set against the final kernel, not only the first-pass
    # ABI. A source/config change between phases must not silently escape this.
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        kernel = temp/'kernel'; vendor = temp/'vendor'
        kernel.mkdir(); vendor.mkdir()
        (kernel/'Image').symlink_to(args.image.resolve())
        (kernel/'Module.symvers').symlink_to(symvers.resolve())
        reference = dict(kernel_release_prefix=initial['modules'][0]['vermagic'].split()[0], vendor={}, gki=[])
        for row in initial['modules']:
            name = row['name']
            if row['kind'] == 'gki':
                (kernel/name).symlink_to((args.frozen/'modules'/name).resolve())
                reference['gki'].append(name)
            else:
                (vendor/name).symlink_to((args.frozen/'modules'/name).resolve())
                reference['vendor'][name] = None
        (temp/'reference.json').write_text(json.dumps(reference))
        subprocess.run([sys.executable, str(HERE/'verify_build.py'), '--kernel', str(kernel),
                        '--modules', str(vendor), '--reference', str(temp/'reference.json'),
                        '--report', str(temp/'ABI.json')], check=True)
    image = args.image.read_bytes()
    cert = args.cert.read_bytes()
    inform = 'PEM' if cert.startswith(b'-----BEGIN') else 'DER'
    der = subprocess.check_output(['openssl', 'x509', '-inform', inform, '-in', str(args.cert), '-outform', 'DER'])
    if der not in image:
        raise ValueError('The final Image does not contain this signing certificate')
    for name, row in manifest['modules'].items():
        body = (args.frozen/'modules'/name).read_bytes()
        if sha(body) != row['payload_sha256'] or len(body) != row['payload_bytes']:
            raise ValueError('Frozen module changed: '+name)
        entry = struct.pack('<Q', len(body))+hashlib.sha256(body).digest()
        if entry not in image:
            raise ValueError('Final Image does not embed the frozen payload entry: '+name)
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('Signing output must be new/empty; no implicit re-signing')
    args.output.mkdir(parents=True, exist_ok=True)
    pem = subprocess.check_output(['openssl', 'x509', '-inform', inform, '-in', str(args.cert), '-outform', 'PEM'])
    certificate = args.output/'signing-certificate.pem'
    certificate.write_bytes(pem)
    rows = {}
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        for name, row in manifest['modules'].items():
            target = args.output/name
            source = args.frozen/'modules'/name
            target.write_bytes(sign_payload(source.read_bytes(), args.key.resolve(), certificate))
            signed = target.read_bytes()
            body = policy.payload(signed)
            if sha(body) != row['payload_sha256']:
                raise ValueError('Signing changed the module ELF payload: '+name)
            sig = signed[len(body):-len(policy.MAGIC)-policy.SIG_STRUCT.size]
            (temp/'signature.der').write_bytes(sig)
            # -nointern prevents an embedded untrusted signer from satisfying
            # the check instead of the final kernel certificate supplied here.
            subprocess.run(['openssl', 'cms', '-verify', '-binary', '-inform', 'DER',
                            '-in', str(temp/'signature.der'), '-content', str(source),
                            '-certfile', str(certificate), '-nointern', '-noverify', '-out', '/dev/null'],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            rows[name] = dict(sha256=sha(signed), payload_sha256=sha(body))
    result = dict(schema='pong-signed-modules-v1', status='SIGNED_PAYLOAD_AND_CERTIFICATE_VERIFIED',
                  modules=rows, count=len(rows), final_kernel_sha256=sha(image), certificate_sha256=sha(der),
                  hardware_tested=False, phone_operations=0, private_key_copied=False)
    result['final_ABI_verified'] = True
    result['signature_force_config_verified'] = True
    (args.output/'manifest.json').write_text(json.dumps(result, indent=2)+'\n')
    print(result['status'], len(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    f = sub.add_parser('freeze')
    for name in ['kernel', 'modules', 'reference', 'output']:
        f.add_argument('--'+name, type=Path, required=True)
    s = sub.add_parser('sign')
    for name in ['frozen', 'image', 'key', 'cert', 'output']:
        s.add_argument('--'+name, type=Path, required=True)
    s.add_argument('--sign-file', type=Path, help='Legacy argument; final signatures use OpenSSL CMS SHA-256')
    args = parser.parse_args()
    (freeze if args.command == 'freeze' else sign)(args)


if __name__ == '__main__':
    main()
