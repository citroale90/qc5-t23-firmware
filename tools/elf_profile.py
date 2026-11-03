"""Inspect project-owned symbols in ELF32 MIPS modules without executing them."""
import struct

def dynamic_symbols(data):
    if data[:7]!=b'\x7fELF\x01\x01\x01' or data[18:20]!=b'\x08\x00':
        raise ValueError('Expected ELF32 little-endian MIPS')
    try:
        offset=struct.unpack_from('<I',data,32)[0]
        size,count=struct.unpack_from('<HH',data,46)
        if size!=40 or not 1<=count<=2048 or offset+size*count>len(data):
            raise ValueError('Invalid ELF section table')
        sections=[struct.unpack_from('<10I',data,offset+i*size)for i in range(count)]
        defined=set();undefined=set()
        for section in sections:
            if section[1]!=11:continue # SHT_DYNSYM
            start,length,link,entry=section[4],section[5],section[6],section[9]
            if entry!=16 or length%16 or start+length>len(data)or link>=count:
                raise ValueError('Invalid ELF dynamic symbols')
            strings_section=sections[link]
            so,sl=strings_section[4:6]
            if strings_section[1]!=3 or so+sl>len(data):raise ValueError('Invalid ELF strings')
            strings=data[so:so+sl]
            for i in range(start,start+length,16):
                name,_,_,_,_,index=struct.unpack_from('<IIIBBH',data,i)
                if name>=sl or b'\0'not in strings[name:]:raise ValueError('Invalid ELF symbol name')
                name=strings[name:].split(b'\0',1)[0].decode('ascii')
                if name:(defined if index else undefined).add(name)
        return defined,undefined
    except (struct.error,UnicodeDecodeError)as error:
        raise ValueError('Incomplete ELF symbol profile')from error

def project_symbols(data):
    defined,undefined=dynamic_symbols(data)
    return ({s for s in defined if s.startswith('qc5_')},
            {s for s in undefined if s.startswith('qc5_')})

def require_private_preload_exports(data):
    defined,_=dynamic_symbols(data)
    unexpected={name for name in defined if not name.startswith('qc5_')}
    if unexpected:raise ValueError('Preload must not interpose compiler/libc functions: '+','.join(sorted(unexpected)))

def require_mips_soft_float(data):
    if data[:7]!=b'\x7fELF\x01\x01\x01' or data[18:20]!=b'\x08\x00':
        raise ValueError('Expected ELF32 little-endian MIPS')
    try:
        start=struct.unpack_from('<I',data,28)[0]
        size,count=struct.unpack_from('<HH',data,42)
        if size!=32 or not 1<=count<=64 or start+size*count>len(data):
            raise ValueError('Invalid MIPS program headers')
        segments=[struct.unpack_from('<8I',data,start+i*size)for i in range(count)]
        abi=next(s for s in segments if s[0]==0x70000003)
        if abi[4]!=24 or abi[1]+24>len(data):raise ValueError('Invalid MIPS ABI flags')
        version,isa,revision,gpr,cpr1,cpr2,fp,*_=struct.unpack_from('<H6B4I',data,abi[1])
        if (version,isa,revision,gpr,cpr1,cpr2,fp)!=(0,32,2,1,0,0,3):
            raise ValueError('Expected MIPS32r2 soft-float ABI')
    except (struct.error,StopIteration)as error:
        raise ValueError('Missing MIPS ABI flags')from error

def require_project_symbols(data,required=()):
    defined,undefined=project_symbols(data)
    if undefined:raise ValueError('Project symbols must be linked into the module: '+','.join(sorted(undefined)))
    missing=set(required)-defined
    if missing:raise ValueError('Required project symbols missing: '+','.join(sorted(missing)))
    return defined
