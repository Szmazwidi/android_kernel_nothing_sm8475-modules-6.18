#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Diskless VM test: 256 QRTR clients, retained bound and cleanup, signatures forced."""
import argparse,hashlib,importlib.util,io,json,os,subprocess,time,re
from pathlib import Path
HERE=Path(__file__).resolve().parent
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['qemu','kernel','signed','output']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--qemu-data',type=Path);p.add_argument('--clang',default='clang');a=p.parse_args()
 os.umask(0o077);a.output.mkdir(parents=True,exist_ok=False)
 out=a.output
 spec=importlib.util.spec_from_file_location('guard_vm',HERE/'test-module-guards.py')
 helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
 subprocess.run([a.clang,'--target=aarch64-linux-gnu','-nostdlib','-static','-fuse-ld=lld','-O2','-fno-builtin','-fno-stack-protector','-Wl,-e,_start',str(HERE/'tests/qrtr-lookup-smoke.c'),'-o',str(out/'init')],check=True)
 archive=io.BytesIO()
 for name in ['dev','proc','sys']:helper.entry(archive,name,0o040755)
 helper.entry(archive,'dev/console',0o020600,rmajor=5,rminor=1)
 helper.entry(archive,'dev/kmsg',0o020600,rmajor=1,rminor=11)
 helper.entry(archive,'init',0o100755,(out/'init').read_bytes())
 helper.entry(archive,'qrtr.ko',0o100644,(a.signed/'qrtr.ko').read_bytes())
 helper.entry(archive,'TRAILER!!!',0)
 (out/'test.cpio').write_bytes(archive.getvalue());log=out/'serial.log'
 cmd=[str(a.qemu)]
 if a.qemu_data:cmd+=['-L',str(a.qemu_data)]
 cmd+=['-no-user-config','-nodefaults','-machine','virt,gic-version=3,virtualization=off','-accel','tcg,thread=multi','-cpu','max','-smp','2','-m','1536','-display','none','-serial','file:'+str(log),'-monitor','none','-nic','none','-no-reboot','-kernel',str(a.kernel),'-initrd',str(out/'test.cpio'),'-append','rdinit=/init panic=0 loglevel=7 console=ttyAMA0,115200 nokaslr']
 start=time.monotonic()
 with (out/'qemu.stderr').open('wb') as stderr:
  proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=stderr)
  while proc.poll() is None and time.monotonic()-start<180:
   if log.exists() and 'PONG_QRTR DONE 1' in log.read_text(errors='replace'):break
   time.sleep(1)
  if proc.poll() is None:
   proc.terminate()
   try:proc.wait(timeout=10)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
 text=log.read_text(errors='replace') if log.exists() else ''
 got={k:int(v) for k,v in re.findall(r'PONG_QRTR (\w+) (-?\d+)',text)}
 expected={'ENFORCE':1,'MODULE':0,'CREATED':256,'FIRST_FAILURE':0,'BOUND_RETAINED':1,'CLEANUP':1,'DONE':1}
 r={'status':'PASS' if got==expected else 'FAIL','actual':got,'expected':expected,'kernel_sha256':hashlib.sha256(a.kernel.read_bytes()).hexdigest(),'qrtr_sha256':hashlib.sha256((a.signed/'qrtr.ko').read_bytes()).hexdigest(),'seconds':round(time.monotonic()-start,2),'disks_attached':0,'external_network':False,'phone_operations':0,'hardware_verified':False}
 (out/'report.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));return 0 if r['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
