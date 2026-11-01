#!/usr/bin/env python3
"""Restore the audited local source changes; invoke from the Android tree root."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.STDOUT)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='validate without changing files')
    args = parser.parse_args()
    root = Path.cwd()
    if not (root / '.repo').is_dir():
        raise RuntimeError('Run from the Android source tree root (containing .repo).')
    patches = Path(__file__).resolve().parent / 'patches'
    series = json.loads((patches / 'series.json').read_text())
    pending = []
    # Validate every project and file before modifying any project.
    for entry in series:
        repo = root / entry['project']
        if git(repo, 'rev-parse', 'HEAD').decode().strip() != entry['revision']:
            raise RuntimeError(f"Wrong revision: {entry['project']}; use the full known-good manifest.")
        patch = patches / entry['patch']
        if digest(patch.read_bytes()) != entry['sha256']:
            raise RuntimeError(f'Patch checksum mismatch: {patch}')
        states = []
        for file in entry['files']:
            target = repo / file['path']
            actual = digest(target.read_bytes()) if target.exists() else None
            if actual == file['after']:
                states.append('applied')
            elif actual == file['before']:
                states.append('pending')
            else:
                raise RuntimeError(f'Unexpected local contents: {target}')
        if all(state == 'applied' for state in states):
            print(f"Already applied: {entry['project']}")
        elif all(state == 'pending' for state in states):
            git(repo, 'apply', '--check', '--whitespace=nowarn', str(patch))
            pending.append((repo, patch, entry))
        else:
            raise RuntimeError(f"Partially applied changes: {entry['project']}; resolve manually.")
    for repo, patch, entry in pending:
        if not args.check:
            git(repo, 'apply', '--whitespace=nowarn', str(patch))
            for file in entry['files']:
                if digest((repo / file['path']).read_bytes()) != file['after']:
                    raise RuntimeError(f"Post-apply checksum mismatch: {entry['project']}/{file['path']}")
        print(f"{'Would apply' if args.check else 'Applied'}: {entry['project']}")
    print('Snapshot patch validation complete.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.output.decode(errors='replace'), file=sys.stderr)
        sys.exit(1)
