#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fail-closed source and module-cohort preflight before Pong 6.18.63 Kleaf builds.

Read-only, no signing, flashing, phone access, or undocumented fallback.
A PASS proves pins and selected source invariants, NOT compiled ABI/Android boot.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

SOURCE_PIN = Path(__file__).resolve().parents[1] / "docs/pong618-6.18.63-source-pin.json"
EXPECTED_COUNTS={"gki":84,"vendor":209,"total":293,"first_stage":100}

def git(path:Path,*args:str)->str:
    p=subprocess.run(["git","-C",str(path),*args],stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE,text=True)
    if p.returncode:
        raise ValueError(f"git {path} {' '.join(args)} failed: {p.stderr.strip()[:320]}")
    return p.stdout.strip()

def demand(test:bool,message:str,checks:list[str])->None:
    if not test:
        raise ValueError(message)
    checks.append(message)

def need(root:Path,rel:str)->str:
    path=root/rel
    if not path.is_file():
        raise ValueError(f"Missing source: {path}")
    return path.read_text(errors="replace")

def kernel_version(makefile:str)->str:
    result={}
    for key in ("VERSION","PATCHLEVEL","SUBLEVEL"):
        pattern=rf"(?m)^{key}\s*=\s*(\d+)\s*$"
        m=re.search(pattern,makefile)
        if not m:
            raise ValueError("Unexpected kernel Makefile field "+key)
        result[key]=int(m.group(1))
    return f"{result['VERSION']}.{result['PATCHLEVEL']}.{result['SUBLEVEL']}"

def audit(workspace:Path, reference:Path, manifest:Path)->dict:
    checks=[]
    workspace=workspace.resolve()
    common=workspace/"common"
    modules=workspace/"devices/pong618"
    demand(common.is_dir() and modules.is_dir(),"Workspace common and devices/pong618 directories present",checks)
    demand(reference.is_file() and manifest.is_file(),"Exact reference and repo manifest present",checks)
    ref=json.loads(reference.read_text())
    pin = json.loads(SOURCE_PIN.read_text())
    projects={p.get("path"):p for p in ET.parse(manifest).getroot().findall(".//project")}
    demand("common" in projects and "devices/pong618" in projects,
           "Manifest includes both source projects",checks)
    src=projects["common"]
    mod=projects["devices/pong618"]
    khead=git(common,"rev-parse","--verify","HEAD^{commit}")
    mhead=git(modules,"rev-parse","--verify","HEAD^{commit}")
    demand(khead == pin["kernel_revision"] == src.get("revision"),
           "Kernel checkout and manifest match the exact reviewed source pin", checks)
    demand(mod.get("revision")==mhead,
           "Modules checkout matches manifest exact SHA, not branch name",checks)
    demand(src.get("upstream") == src.get("dest-branch") == pin["kernel_branch"],
           "Kernel manifest uses the reviewed branch", checks)
    for parent, label in ((pin["kernel_base"], "Android kernel base"),
                          (pin["upstream_stable"], "Linux v6.18.63")):
        git(common, "merge-base", "--is-ancestor", parent, khead)
        checks.append(f"{label} is in the kernel history")
    git(modules, "merge-base", "--is-ancestor", pin["module_base"], mhead)
    checks.append("CLG74 is in the module history")
    demand(git(common,"status","--porcelain","--untracked-files=no")=="",
           "No uncommitted tracked kernel changes",checks)
    demand(git(modules,"status","--porcelain","--untracked-files=no")=="",
           "No uncommitted tracked module sources",checks)
    demand(kernel_version(need(common,"Makefile"))=="6.18.63",
           "Kernel Makefile identifies Linux 6.18.63",checks)
    demand(ref.get("kernel_release_prefix")=="6.18.63-" and
           ref.get("kernel_profile")=="community",
           "6.18.63 community profile with correct release prefix",checks)
    demand(ref.get("expected_counts")==EXPECTED_COUNTS,
           "CLG74 requires 209 vendor + 84 GKI and 100 first-stage",checks)
    vendor,gki=ref.get("vendor",{}),ref.get("gki",{})
    demand(len(vendor)==209 and len(gki)==84 and not(set(vendor)&set(gki)),
           "Complete, non-overlapping 293-module bank",checks)
    first=modules/ref.get("first_stage_list","")
    demand(first.is_file(),"First-stage list found",checks)
    digest=hashlib.sha256(first.read_bytes()).hexdigest()
    demand(digest==ref.get("first_stage_list_sha256"),
           "First-stage list SHA256 verified",checks)

    def has(path:str,token:str)->bool:
        return token in need(common,path)
    def lacks(path:str,token:str)->bool:
        return token not in need(common,path)
    demand(has("arch/arm64/configs/gki_defconfig","CONFIG_MODULE_SIG_FORCE=y") and
           has("arch/arm64/configs/gki_defconfig","CONFIG_MODULE_SIG_SHA256=y"),
           "Forced SHA-256 module signature admission enabled",checks)
    demand(lacks("arch/arm64/configs/gki_defconfig","CONFIG_PONG_LAB_GUARDS=y"),
           "No LAB-only protection enabled in community defconfig",checks)
    demand(has("arch/arm64/configs/pong_lab.fragment","CONFIG_PONG_LAB_GUARDS=y"),
           "Preserved separate PONG LAB guard fragment",checks)
    for name in range(1,5):
        demand(has("include/linux/device/bus.h",f"ANDROID_KABI_RESERVE({name});"),
               f"Android bus_type KMI reserve {name} remains",checks)
    io=need(common,"io_uring/io_uring.c")
    demand("io_queue_iowq_foreign_tw" not in io and "tw.cancel" not in io and
           "XA_FLAGS_ALLOC1 | XA_FLAGS_ACCOUNT" in io and
           "if ((current->flags & PF_KTHREAD) || !tctx->io_wq)" in io,
           "Retained Android io_uring task-work ABI plus stable xarray accounting",checks)
    demand(has("kernel/sched/ext.c","scx_disable(SCX_EXIT_UNREG);") and
           lacks("kernel/sched/ext.c","scx_disable(sch, SCX_EXIT_UNREG);"),
           "sched_ext unregistration matches retained Android API",checks)
    xh=need(common,"drivers/usb/host/xhci.h")
    start=xh.find("struct xhci_port {")
    end=xh.find("};",start)
    demand(start>=0 and end>start and
           "lpm_incapable:1" in xh[start:end] and "link_inactive:1" in xh[start:end] and
           xh[start:end].index("lpm_incapable:1")<xh[start:end].index("link_inactive:1"),
           "xHCI port bitfield order preserves Android 6.18.60 layout",checks)
    release = need(common, "drivers/media/rc/rc-main.c")
    release = release.split("static void rc_dev_release(struct device *device)", 1)[-1].split("\n}", 1)[0]
    demand(release.count("ir_raw_event_free(dev);") == 1 and
           "ir_raw_event_free(dev);" in release.split("kfree(dev);", 1)[0],
           "IR raw state is freed once before the rc_dev allocation", checks)
    vidc = need(modules, "vidcCLS18/driver/vidc/src/msm_vidc.c")
    query = vidc.split("int msm_vidc_query_ctrl(", 1)[-1].split("\n}", 1)[0]
    demand("q_ctrl->type = ctrl->type;" in query and
           "strscpy(q_ctrl->name, ctrl->name" in query,
           "VIDC ioctl retains control type and name for legacy conversion", checks)
    demand(lacks("drivers/media/rc/ene_ir.c","rc_free_device(dev->rdev);"),
           "No duplicate rc_dev put in ene_ir remove",checks)
    demand(has("rust/kernel/id_pool.rs","pub fn find_unused_id(") and
           has("rust/kernel/id_pool.rs","pub struct UnusedId") and
           has("rust/kernel/bitmap.rs","pub fn new_inline() -> Self"),
           "Android Rust ID pool API and inline bitmap constructor restored",checks)
    demand(has("drivers/remoteproc/remoteproc_core.c","trace_android"),
           "Android remoteproc trace hook preserved",checks)
    demand(has("drivers/slimbus/messaging.c","goto slim_xfer_err;"),
           "Upstream SLIMbus error-path fix present",checks)
    return {
      "stage_status":"HOST_SOURCE_PREFLIGHT_PASSED_BUILD_NOT_STARTED",
      "evidence_level":"HOST_VERIFIED_STATIC_ONLY",
      "kernel_sha":khead,"modules_sha":mhead,
      "release":"6.18.63","profile":"community","module_counts":EXPECTED_COUNTS,
      "first_stage_sha256":digest,
      "checks_passed":checks,
      "remaining_blockers":[
        "Full Android-pinned Kleaf kernel Image and BTF",
        "Fresh compilation of 209 vendor and 84 GKI modules",
        "KMI/symbol CRC and signing verification",
        "nf_conntrack_expect timer_list -> u32 layout change: inspect BTF/CRC",
        "QEMU positive/negative module loading and recovery preflight",
        "Physical Android 17 sys.boot_completed=1 on 6.18.63"
      ],
      "historical_reuse_audit":{
        "6_18_60":[
          "PONG U59 014f1f8 (io_uring Android r5 type/KMI)",
          "PONG U59 64a21b3 (xHCI ACK r5 KMI)",
          "PONG U59 f591bb6 (netdev queue configs)"
        ],
        "5_15":"Historical index checked for ir_raw_event_free, io_tw_req, link_inactive: zero exact-text hits; no binary patch reused"
      },
      "performed_phone_operations":[],
      "protected_partitions_unchanged":"NO_PHONE_OPERATIONS_PERFORMED",
      "recovery_status":"NOT_REQUALIFIED"
    }

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--workspace",type=Path,default=Path("."))
    ap.add_argument("--reference",type=Path,required=True)
    ap.add_argument("--manifest",type=Path,required=True)
    ap.add_argument("--report",type=Path,required=True)
    args=ap.parse_args()
    result=None
    try:
        result=audit(args.workspace,args.reference,args.manifest)
        code=0
    except (ValueError,KeyError,OSError,ET.ParseError,subprocess.SubprocessError) as exc:
        result={"stage_status":"FAILED_PREBUILD_SOURCE_QUALIFICATION",
                "evidence_level":"FAILED",
                "reason":str(exc),
                "performed_phone_operations":[],
                "protected_partitions_unchanged":"NO_PHONE_OPERATIONS_PERFORMED",
                "recovery_status":"NOT_REQUALIFIED"}
        code=2
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n")
    print(json.dumps(result,indent=2,ensure_ascii=False))
    sys.exit(code)
if __name__=="__main__":
    main()
