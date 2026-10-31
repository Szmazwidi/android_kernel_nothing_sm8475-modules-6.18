#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check source-build coverage, architecture, vermagic and symbol-version closure.

Standard library only. This checks build artifacts; it never accesses a phone.
An unsigned vendor module is expected here and still requires final signing.
"""
import argparse, collections, hashlib, json, pathlib, struct

def sections(path):
    data=path.read_bytes()
    assert data[:6]==b'\x7fELF\x02\x01'and struct.unpack_from('<H',data,18)[0]==183,('Not AArch64 ELF64',str(path))
    offset=struct.unpack_from('<Q',data,40)[0]
    size,count,names=struct.unpack_from('<HHH',data,58)
    assert size==64 and count and names<count
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*size)for i in range(count)]
    strings=data[headers[names][4]:headers[names][4]+headers[names][5]]
    result={}
    for h in headers:
        end=strings.find(b'\0',h[0]);name=strings[h[0]:end].decode()
        result[name]=data[h[4]:h[4]+h[5]]
    return data,result

def versions(sec):
    if '__versions'in sec:
        data=sec['__versions'];assert len(data)%64==0
        return {data[i+8:i+64].split(b'\0')[0].decode():struct.unpack_from('<Q',data,i)[0]&0xffffffff for i in range(0,len(data),64)}
    if '__version_ext_crcs'in sec:
        names=sec['__version_ext_names'].rstrip(b'\0').split(b'\0');crcs=sec['__version_ext_crcs']
        assert len(crcs)==4*len(names)
        return {n.decode():struct.unpack_from('<I',crcs,4*i)[0]for i,n in enumerate(names)}
    raise ValueError('No module symbol versions')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['kernel','modules','vendor-symvers','reference','report']:p.add_argument('--'+key,type=pathlib.Path,required=True)
    a=p.parse_args();reference=json.loads(a.reference.read_text())
    # kernel_aarch64_dist names it kernel_aarch64_Module.symvers
    kernel_symvers=next((a.kernel/n for n in ['Module.symvers','kernel_aarch64_Module.symvers']if(a.kernel/n).is_file()),None)
    assert (a.kernel/'Image').is_file()and kernel_symvers,'Image or Module.symvers missing in --kernel'
    assert (a.kernel/'Image').read_bytes()[56:60]==b'ARMd','Not an ARM64 Image'
    found={f.name:f for f in a.modules.glob('*.ko')};expected=set(reference['vendor'])
    errors=[]
    if set(found)!=expected:errors.append(dict(coverage_missing=sorted(expected-set(found)),coverage_extra=sorted(set(found)-expected)))
    gki_missing=sorted(n for n in reference['gki']if not(a.kernel/n).exists())
    if gki_missing:errors.append(dict(missing_reference_GKI=gki_missing))
    symbols=collections.defaultdict(set);seen=set()
    files=[kernel_symvers]+list(a.vendor_symvers.rglob('*Module.symvers'))
    for f in files:
        text=f.read_bytes();key=hashlib.sha256(text).digest()
        if key in seen:continue
        seen.add(key)
        for line in text.decode().splitlines():
            parts=line.split()
            if len(parts)>=3:symbols[parts[1]].add(int(parts[0],16)&0xffffffff)
    rows=[]
    for name,f in sorted(found.items()):
        data,sec=sections(f);info={}
        for field in sec.get('.modinfo',b'').split(b'\0'):
            if b'='in field:
                k,v=field.split(b'=',1);info[k.decode()]=v.decode(errors='replace')
        if not info.get('vermagic','').startswith(reference['kernel_release_prefix']):errors.append(dict(module=name,bad_vermagic=info.get('vermagic')))
        imports=versions(sec);bad={n:dict(module_crc=hex(crc),provider_crcs=sorted(hex(v)for v in symbols[n]))for n,crc in imports.items()if crc not in symbols[n]}
        if bad:errors.append(dict(module=name,symbol_version_errors=bad))
        rows.append(dict(name=name,sha256=hashlib.sha256(data).hexdigest(),vermagic=info.get('vermagic'),imports=len(imports),signed=data.endswith(b'~Module signature appended~\n')))
    result=dict(status='SOURCE_BUILD_COVERAGE_ARCH_VERSION_CRC_PASS'if not errors else'SOURCE_BUILD_VALIDATION_FAILED',
                vendor_modules=len(rows),reference_GKI_modules=len(reference['gki']),distinct_symvers_inputs=len(seen),
                kernel_sha256=hashlib.sha256((a.kernel/'Image').read_bytes()).hexdigest(),modules=rows,errors=errors,
                hardware_tested=False,phone_operations=0)
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items()if k not in ['modules','errors']}))
    if errors:
        print(json.dumps(errors[:3],indent=2));raise SystemExit(1)
if __name__=='__main__':main()
