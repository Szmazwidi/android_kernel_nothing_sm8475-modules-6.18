#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile the actual encoder and compare its output with IEEE754 golden values."""
from pathlib import Path
import argparse, json, struct, subprocess, tempfile
H=Path(__file__).resolve().parent
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--source',type=Path,default=H.parent/'thermalCLG8/qmi_sensors.c')
ap.add_argument('--report',type=Path)
args=ap.parse_args()
source=args.source.read_text()
encoder=source[source.index('static int32_t encode_qmi('):source.index('static int32_t decode_qmi(')]
prefix='''#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define QMI_FL_SIGN_BIT 31
#define QMI_MANTISSA_MSB 23
#define pr_debug(...) ((void)0)
static unsigned find_last_bit(const unsigned long *v, unsigned n) {
 return *v ? sizeof(*v)*8-1-__builtin_clzl(*v) : n;
}
'''
main='''int main(int argc,char **argv) {
 for(int i=1;i<argc;i++)printf("%08x\\n",(uint32_t)encode_qmi((int32_t)strtol(argv[i],0,10)));
 return 0;
}
'''
values=list(range(-273000,301000,1000))+[-999,-1,0,1,999,2147483647,-2147483647,-2147483648]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(prefix+encoder+main)
    subprocess.run(['cc','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True,capture_output=True)
    q=subprocess.run([str(p/'test'),*map(str,values)],check=True,capture_output=True,text=True)
actual=q.stdout.splitlines()
for value,result in zip(values,actual,strict=True):
    expected=0 if abs(value)>=2147483647 else struct.unpack('<I',struct.pack('<f',int(value/1000)))[0]
    assert int(result,16)==expected,(value,result,hex(expected))
report={'status':'IEEE754_GOLDEN_AND_UBSAN_PASS','cases':len(values),'scope':'Actual extracted candidate encoder; no QMI transport or device mitigation test.'}
if args.report: args.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
