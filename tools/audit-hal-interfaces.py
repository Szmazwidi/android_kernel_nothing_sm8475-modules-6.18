#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only audit of a Pong device checkout and an assembled Android product.

VINTF declarations and installed files are build evidence, not proof that a HAL
starts or that enrollment, payments, calls, camera or audio work on a phone.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

FINGERPRINT = 'android.hardware.biometrics.fingerprint'
PROVIDERS = {
    'aidl': (FINGERPRINT + '-service.nothing', 'fingerprint', 'libudfps_extension.nothing'),
    'hidl': (FINGERPRINT + '@2.3-service.nt.pong', 'hidl/fingerprint', 'libudfps_extension.nt.pong'),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def revision(path):
    try:
        return subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'],
                                       stderr=subprocess.DEVNULL, text=True).strip()
    except subprocess.CalledProcessError:
        return None


def tracked_dirty(path):
    try:
        return bool(subprocess.check_output(['git', '-C', str(path), 'diff', '--name-only', 'HEAD'],
                                            stderr=subprocess.DEVNULL))
    except subprocess.CalledProcessError:
        return None


def audit(device, product, expect=None):
    errors, rows = [], []
    # Limit the scan to assembled VINTF inputs, never user data or kernel outputs.
    for partition in ['vendor', 'odm', 'system', 'system_ext', 'product']:
        for path in sorted((product/partition/'etc/vintf').rglob('*.xml')):
            tree = ET.parse(path).getroot()
            if tree.tag != 'manifest':
                continue
            for hal in tree.findall('hal'):
                rows.append(dict(partition=partition, file=str(path.relative_to(product)),
                                 format=hal.get('format', 'hidl'), name=hal.findtext('name'),
                                 versions=[e.text for e in hal.findall('version')],
                                 fqnames=[e.text for e in hal.findall('fqname')],
                                 interfaces=[dict(name=i.findtext('name'),
                                                  instances=[e.text for e in i.findall('instance')])
                                             for i in hal.findall('interface')]))
    if not rows:
        errors.append('No assembled VINTF manifest entries found')
    selected = []
    make = re.sub(r'#[^\n]*', '', (device/'device.mk').read_text())
    for kind, (binary, directory, extension) in PROVIDERS.items():
        if re.search(r'(?m)^\s*' + re.escape(binary) + r'\s*(?:\\)?\s*$', make):
            selected.append(kind)
            if extension not in make:
                errors.append('Fingerprint SurfaceFlinger extension does not match '+kind)
    if len(selected) != 1:
        errors.append('Expected one fingerprint PRODUCT_PACKAGES selection: '+repr(selected))
    declared = [h for h in rows if h['name'] == FINGERPRINT]
    if len(declared) != 1:
        errors.append('Expected one standard fingerprint VINTF declaration, found '+str(len(declared)))
    elif selected and declared[0]['format'] != selected[0]:
        errors.append('Built fingerprint VINTF format differs from the source selection')
    if expect and selected != [expect]:
        errors.append('Fingerprint selection differs from --expect-fingerprint '+expect)
    installed, source = {}, {}
    for kind, (binary, directory, _) in PROVIDERS.items():
        for relative in ['vendor/bin/hw/'+binary, 'vendor/etc/init/'+binary+'.rc',
                         'vendor/etc/vintf/manifest/'+binary+'.xml']:
            path = product/relative
            if path.is_file():
                installed[relative] = digest(path)
            if kind in selected and not path.is_file():
                errors.append('Selected fingerprint artifact missing: '+relative)
            if kind not in selected and path.is_file():
                errors.append('Unselected fingerprint artifact installed: '+relative)
        for path in sorted((device/directory).rglob('*')):
            if path.is_file() and path.suffix in {'.bp', '.cpp', '.h', '.rc', '.xml', '.sysprop'}:
                source[str(path.relative_to(device))] = digest(path)
    return dict(schema='pong-hal-build-audit-v1', status='PASS' if not errors else 'FAIL',
                device_commit=revision(device), device_tracked_dirty=tracked_dirty(device),
                fingerprint_selected=selected,
                fingerprint_vintf=declared, installed_fingerprint_sha256=installed,
                source_fingerprint_sha256=source,
                declaration_counts=dict(collections.Counter(h['format'] for h in rows)),
                declarations=rows, errors=errors, runtime_tested=False, phone_operations=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-tree', type=Path, required=True)
    parser.add_argument('--product', type=Path, required=True)
    parser.add_argument('--expect-fingerprint', choices=PROVIDERS)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.device_tree, args.product, args.expect_fingerprint)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k: result[k] for k in ['status', 'fingerprint_selected', 'declaration_counts', 'errors']}))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
