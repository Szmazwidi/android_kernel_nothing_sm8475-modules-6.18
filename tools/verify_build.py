#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check source-build coverage, architecture, vermagic and symbol-version closure.

Standard library only. This checks build artifacts; it never accesses a phone.
An unsigned vendor module is expected here and still requires final signing.
"""
import argparse, collections, hashlib, json, pathlib, re, struct

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

def export_crcs(data):
    """Read actual exported CRC words, rather than trust cached DDK symvers."""
    offset=struct.unpack_from('<Q',data,40)[0]
    size,count,_=struct.unpack_from('<HHH',data,58)
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*size)for i in range(count)]
    exports={}
    for header in headers:
        if header[1]!=2:continue  # SHT_SYMTAB
        strings_header=headers[header[6]]
        names=data[strings_header[4]:strings_header[4]+strings_header[5]]
        if header[9]!=24:raise ValueError('Invalid symbol size')
        for pos in range(header[4],header[4]+header[5],24):
            index,_,_,section,value,_=struct.unpack_from('<IBBHQQ',data,pos)
            name=names[index:names.find(b'\0',index)].decode()
            if not name.startswith('__crc_'):continue
            if section==0xfff1:  # SHN_ABS (older modpost)
                crc=value&0xffffffff
            elif 0<section<len(headers):
                source=headers[section]
                if value+4>source[5]:raise ValueError('Invalid CRC location: '+name)
                crc=struct.unpack_from('<I',data,source[4]+value)[0]
            else:raise ValueError('Undefined exported CRC: '+name)
            exports[name[6:]]=crc
    return exports

def runtime_contract_errors(data, sec, contract):
    """Require runtime interfaces that a symbol-CRC-only audit cannot prove.

    In particular a vmap-capable diagnostic heap can compile and link while
    lacking the userspace mmap callback required by Android gralloc.
    """
    offset=struct.unpack_from('<Q',data,40)[0]
    size,count,_=struct.unpack_from('<HHH',data,58)
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*size)for i in range(count)]
    functions=set()
    for h in headers:
        if h[1]!=2:continue
        strings=headers[h[6]]
        names=data[strings[4]:strings[4]+strings[5]]
        for pos in range(h[4],h[4]+h[5],24):
            index,info,_,section,_,_=struct.unpack_from('<IBBHQQ',data,pos)
            if info&15==2 and section:
                functions.add(names[index:names.find(b'\0',index)].decode())
    missing_functions=sorted(set(contract.get('required_functions',[]))-functions)
    missing_imports=sorted(set(contract.get('required_imports',[]))-set(versions(sec)))
    missing_strings=[s for s in contract.get('required_strings',[])if s.encode()+b'\0'not in data]
    if missing_functions or missing_imports or missing_strings:
        return dict(missing_functions=missing_functions,missing_imports=missing_imports,missing_strings=missing_strings)
    return {}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['kernel','modules','reference','report']:p.add_argument('--'+key,type=pathlib.Path,required=True)
    p.add_argument('--vendor-symvers',type=pathlib.Path,help='Legacy argument; stale DDK symvers are deliberately not used')
    a=p.parse_args();reference=json.loads(a.reference.read_text())
    # kernel_aarch64_dist names it kernel_aarch64_Module.symvers
    kernel_symvers=next((a.kernel/n for n in ['Module.symvers','kernel_aarch64_Module.symvers']if(a.kernel/n).is_file()),None)
    assert (a.kernel/'Image').is_file()and kernel_symvers,'Image or Module.symvers missing in --kernel'
    kernel_image=(a.kernel/'Image').read_bytes()
    assert kernel_image[56:60]==b'ARMd','Not an ARM64 Image'
    banner=re.search(rb'Linux version ([0-9][^ \x00\n]+)',kernel_image)
    assert banner,'Kernel release banner missing from Image'
    kernel_release=banner[1].decode()
    found={f.name:f for f in a.modules.glob('*.ko')};expected=set(reference['vendor'])
    errors=[]
    if set(found)!=expected:errors.append(dict(coverage_missing=sorted(expected-set(found)),coverage_extra=sorted(set(found)-expected)))
    gki_missing=sorted(n for n in reference['gki']if not(a.kernel/n).exists())
    if gki_missing:errors.append(dict(missing_reference_GKI=gki_missing))
    # vmlinux is the only built-in provider. Every other provider must exist in
    # the selected module set, with a CRC read from its actual ELF __crc_* symbol.
    # In particular a stale or unselected Module.symvers cannot satisfy an import.
    symbols=collections.defaultdict(set)
    for line in kernel_symvers.read_text().splitlines():
        parts=line.split()
        if len(parts)>=3 and parts[2]=='vmlinux':symbols[parts[1]].add(int(parts[0],16)&0xffffffff)
    collisions=set(found)&set(reference['gki'])
    if collisions:errors.append(dict(vendor_GKI_name_collisions=sorted(collisions)))
    selected={**{n:a.kernel/n for n in reference['gki']if(a.kernel/n).is_file()},**found}
    for f in selected.values():
        data,_=sections(f)
        for name,crc in export_crcs(data).items():symbols[name].add(crc)
    rows=[];vermagics=set()
    normalized_names={n.removesuffix('.ko').replace('-','_')for n in selected}
    for name,f in sorted(selected.items()):
        data,sec=sections(f);info={}
        for field in sec.get('.modinfo',b'').split(b'\0'):
            if b'='in field:
                k,v=field.split(b'=',1);info[k.decode()]=v.decode(errors='replace')
        if not info.get('vermagic','').startswith(reference['kernel_release_prefix']):errors.append(dict(module=name,bad_vermagic=info.get('vermagic')))
        magic=info.get('vermagic','')
        if not magic or magic.split()[0]!=kernel_release:errors.append(dict(module=name,kernel_release_mismatch=magic,expected_release=kernel_release))
        vermagics.add(magic)
        contract=reference.get('runtime_contracts',{}).get(name)
        if contract:
            failures=runtime_contract_errors(data,sec,contract)
            if failures:errors.append(dict(module=name,runtime_contract_errors=failures))
        imports=versions(sec);bad={n:dict(module_crc=hex(crc),provider_crcs=sorted(hex(v)for v in symbols[n]))for n,crc in imports.items()if crc not in symbols[n]}
        if bad:errors.append(dict(module=name,symbol_version_errors=bad))
        missing_deps=[n for n in info.get('depends','').split(',')if n and n.replace('-','_')not in normalized_names]
        if missing_deps:errors.append(dict(module=name,missing_dependencies=missing_deps))
        rows.append(dict(name=name,kind='vendor'if name in found else'gki',sha256=hashlib.sha256(data).hexdigest(),vermagic=info.get('vermagic'),imports=len(imports),signed=data.endswith(b'~Module signature appended~\n')))
    if len(vermagics)>1:errors.append(dict(mixed_vermagic=sorted(vermagics)))
    result=dict(status='SOURCE_BUILD_COVERAGE_ARCH_VERSION_CRC_PASS'if not errors else'SOURCE_BUILD_VALIDATION_FAILED',
                schema='pong-source-build-check-v2',vendor_modules=len(found),reference_GKI_modules=len(reference['gki']),selected_modules=len(rows),
                kernel_release=kernel_release,
                provider_source='vmlinux symvers plus selected ELF __crc_* symbols',
                kernel_sha256=hashlib.sha256((a.kernel/'Image').read_bytes()).hexdigest(),modules=rows,errors=errors,
                hardware_tested=False,phone_operations=0)
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items()if k not in ['modules','errors']}))
    if errors:
        print(json.dumps(errors[:3],indent=2));raise SystemExit(1)
if __name__=='__main__':main()
