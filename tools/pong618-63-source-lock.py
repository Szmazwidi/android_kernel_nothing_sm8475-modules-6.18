#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate an exact-source CLG74/6.18.63 manifest and module reference.

Never flashes or signs images. Requires a clean, *already integrated*
6.18.63 kernel tree. Rejects branch-name-only and version-string-only changes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
BASE_XML = ROOT / "docs/manifest/pong618-6.18.60-community.xml"
BASE_JSON = ROOT / "docs/build-reference-6.18.60-community.json"
NEW_XML = ROOT / "docs/manifest/pong618-6.18.63-community-clg74.xml"
NEW_JSON = ROOT / "docs/build-reference-6.18.63-community-clg74.json"
EXPECTED_KERNEL_BASE = "58b289d57b3d1f4b6fa6bb51db13920f459ae153"
EXPECTED_MODULE_MANIFEST = "bad6c14720da8364280add901bb549b78f30b5b8"
CLG74_BASE = "935e1cf587ada4e684c48b89f04f93cd4b5ad86f"
UPSTREAM_STABLE = "1cdda838e8475edf89b73ff0cc952005dd0b1fa7"

def fail(message: str) -> None:
    raise SystemExit("STOP: " + message)

def git(repo: Path, *args: str) -> str:
    cmd = subprocess.run(["git", "-C", str(repo), *args],
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if cmd.returncode:
        fail("git %s: %s" % (" ".join(args), cmd.stderr.strip()))
    return cmd.stdout.strip()

def verify_base() -> tuple[ET.ElementTree, dict]:
    if not BASE_XML.is_file() or not BASE_JSON.is_file():
        fail("Missing pinned 6.18.60 reference files")
    root = ET.parse(BASE_XML)
    projects = {p.get("path"): p for p in root.findall(".//project")}
    if projects.get("common") is None or projects.get("devices/pong618") is None:
        fail("Manifest missing source project mappings")
    if projects["common"].get("revision") != EXPECTED_KERNEL_BASE:
        fail("Unexpected 6.18.60 kernel base in source manifest")
    if projects["devices/pong618"].get("revision") != EXPECTED_MODULE_MANIFEST:
        fail("CLG43 source manifest changed; review pin manually")
    ref = json.loads(BASE_JSON.read_text())
    counts = ref.get("expected_counts", {})
    if (ref.get("kernel_release_prefix"), ref.get("kernel_profile"),
        len(ref.get("vendor", {})), len(ref.get("gki", {})),
        counts.get("first_stage")) != ("6.18.60-", "community", 209, 84, 100):
        fail("Expected 209+84 community bank / 100 first stage is absent")
    first_stage = ROOT / ref["first_stage_list"]
    if not first_stage.is_file():
        fail("First-stage list missing: " + str(first_stage))
    observed = hashlib.sha256(first_stage.read_bytes()).hexdigest()
    if observed != ref["first_stage_list_sha256"]:
        fail("First-stage list SHA256 does not match source reference")
    return root, ref

def write_once(path: Path, data: bytes) -> str:
    digest = hashlib.sha256(data).hexdigest()
    if path.exists():
        if path.read_bytes() != data:
            fail("Existing generated file differs: " + str(path))
        print("UNCHANGED", path, digest)
    else:
        with path.open("xb") as stream:
            stream.write(data)
        print("CREATED", path, digest)
    return digest

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lint-only", action="store_true",
                    help="Check the inherited CLG74 manifest and reference only")
    ap.add_argument("--kernel-tree", type=Path,
                    help="Clean worktree on pong-a17-6.18.63-integration")
    args = ap.parse_args()

    tree, ref = verify_base()
    print("BASELINE_OK vendor=209 gki=84 first_stage=100 base_kernel=6.18.60")
    if args.lint_only:
        return
    if not args.kernel_tree:
        fail("Provide --kernel-tree after actual stable source integration")
    kernel = args.kernel_tree.resolve()
    if not (kernel / "Makefile").is_file():
        fail("Kernel source tree not present")
    kernel_sha = git(kernel, "rev-parse", "--verify", "HEAD^{commit}")
    module_sha = git(ROOT, "rev-parse", "--verify", "HEAD^{commit}")
    git(ROOT, "merge-base", "--is-ancestor", CLG74_BASE, module_sha)
    git(kernel, "merge-base", "--is-ancestor", EXPECTED_KERNEL_BASE, kernel_sha)
    git(kernel, "merge-base", "--is-ancestor", UPSTREAM_STABLE, kernel_sha)
    if git(kernel, "status", "--porcelain"):
        fail("Dirty integrated kernel tree")
    if git(ROOT, "status", "--porcelain"):
        fail("Dirty modules source tree")
    version = subprocess.run(["make", "-s", "-C", str(kernel), "kernelversion"],
                             capture_output=True, text=True, check=True).stdout.strip()
    if version != "6.18.63":
        fail("Kernel is not 6.18.63: " + version)

    projects = {p.get("path"): p for p in tree.findall(".//project")}
    projects["common"].set("revision", kernel_sha)
    projects["common"].set("upstream", "pong-a17-6.18.63-integration")
    projects["common"].set("dest-branch", "pong-a17-6.18.63-integration")
    projects["devices/pong618"].set("revision", module_sha)
    projects["devices/pong618"].set("upstream", "pong-a17-6.18.63-prep")
    projects["devices/pong618"].set("dest-branch", "pong-a17-6.18.63-prep")
    ET.indent(tree, space="  ")
    xml_data = ET.tostring(tree.getroot(), encoding="utf-8", xml_declaration=True) + b"\n"

    ref["kernel_release_prefix"] = "6.18.63-"
    ref["reference"] = "CLG74_293_community_6.18.63_CANDIDATE"
    ref["runtime_reference"] = "Source-only 6.18.63 candidate; no build, QEMU, device or ROM qualification"
    json_data = (json.dumps(ref, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    xml_sha = write_once(NEW_XML, xml_data)
    json_sha = write_once(NEW_JSON, json_data)
    print(json.dumps({
      "stage_status": "GENERATED_SOURCE_LOCK_BUILD_PENDING",
      "evidence_level": "HOST_VERIFIED_SOURCE_ONLY",
      "kernel_sha": kernel_sha, "modules_sha": module_sha,
      "kernelversion": version, "vendor": 209, "gki": 84,
      "manifest_sha256": xml_sha, "reference_sha256": json_sha,
      "next": "repo sync, full build-release.sh, ABI/signature/QEMU and recovery preflight",
      "phone_operations": 0
    }, indent=2))

if __name__ == "__main__":
    main()
