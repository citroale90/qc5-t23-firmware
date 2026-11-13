"""Build a stock-read-sized self-decompressing uImage, no staging or writer."""
from pathlib import Path
import hashlib,json,lzma,runpy,struct,subprocess,zlib
HERE=Path(__file__).resolve().parent;I=HERE.parents[1];ROOT=I.parent;WS=ROOT.parents[3];BASE=I/'native-services';OUT=HERE/'kernel-build'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate_initramfs(archive):
 raw=lzma.decompress(archive);at=0;names=set();init=None
 while True:
  if at+110>len(raw) or raw[at:at+6]!=b'070701':raise ValueError('initramfs must decode directly to complete newc CPIO')
  try:fields=[int(raw[at+6+8*n:at+14+8*n],16) for n in range(13)]
  except ValueError:raise ValueError('Invalid CPIO header') from None
  size,length=fields[6],fields[11]
  if not 1<=length<=4096 or at+110+length>len(raw):raise ValueError('Invalid CPIO filename length')
  encoded=raw[at+110:at+110+length]
  if not encoded.endswith(b'\0') or b'\0' in encoded[:-1]:raise ValueError('Invalid CPIO filename')
  name=encoded[:-1].decode('ascii');at=(at+110+length+3)&~3
  if at+size>len(raw):raise ValueError('Truncated CPIO body')
  body=raw[at:at+size];at=(at+size+3)&~3
  if name=='TRAILER!!!':
   if size or any(raw[at:]):raise ValueError('Invalid CPIO trailer')
   break
  if name in names or name.startswith('/') or '..' in name.split('/'):raise ValueError('Unsafe or duplicate CPIO filename')
  names.add(name)
  if name=='init':
   if fields[1]&0o170000!=0o100000 or not fields[1]&0o111 or fields[2:4]!=[0,0] or fields[4]!=1 or not body.startswith(b'\x7fELF'):
    raise ValueError('Expected an executable root-owned ELF /init')
   init=body
 if init is None:raise ValueError('Missing /init')
 return {'entries':len(names),'init_sha256':hashlib.sha256(init).hexdigest(),'single_XZ_to_newc':True}
def main():
 OUT.mkdir(exist_ok=True);store=HERE/'root-store';s=json.loads((store/'build.json').read_text())
 for p,h in s['sources'].items():assert sha(Path(p))==h,p
 original=BASE/'candidate';b=json.loads((original/'build.json').read_text());raw=(original/b['filename']).read_bytes();assert sha(original/b['filename'])==b['sha256']
 data=bytearray(lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(raw[64:]));load=struct.unpack_from('>I',raw,16)[0];assert load==0x80100000
 offset=int(b['initrd_start'],16)-load;archive=(store/'initrd.xz').read_bytes();assert len(archive)<b['initramfs_bytes']
 # The kernel unpacks one compression layer into CPIO. A nested XZ stream
 # can pass the outer kernel decoder tests yet cannot supply /init.
 validate_initramfs(archive)
 data[offset:offset+b['initramfs_bytes']]=archive+bytes(b['initramfs_bytes']-len(archive))
 # The same stock-compatible shim, CP0 guard and early PHY code remain.
 # Change only the DTB's end of initrd; its public board profile is retained.
 dtb=OUT/'net-t23.dtb';dtb.write_bytes((original/'net-t23.dtb').read_bytes());subprocess.run(['fdtput','-t','x',str(dtb),'/chosen','linux,initrd-end',f'{((int(b["initrd_start"],16)&0x1fffffff)+len(archive)):x}'],check=True)
 olddtb=(original/'net-t23.dtb').read_bytes();at=data.index(olddtb);assert len(dtb.read_bytes())==len(olddtb);data[at:at+len(olddtb)]=dtb.read_bytes()
 # Production cannot depend on diagnostic raw-SD checkpoints. Keep only
 # watchdog/OST/UHI setup and the already qualified USB PHY preparation.
 wifi=runpy.run_path(str(ROOT/'wifi-net/build-net.py'))['WIFI'];shim=0x80880000;phy=0x80883000
 trace=wifi['mips_call'](phy)+struct.pack('<4I',0x14400003,0,0x1000ffff,0)
 boot=wifi['PKG']['shim'](int(b['entry'],16),shim+0x100,watchdog_ticks=54931,trace=trace,t23_ost_gate=True,native_reset_watchdog=True)
 assert len(boot)<=0x100;start=shim-load;data[start:start+0x100]=boot+bytes(0x100-len(boot))
 data[0x80881000-load:0x80882000-load]=bytes(0x1000)
 guard_report=json.loads((HERE/'cpu-guard/build.json').read_text());assert guard_report['compiled']
 guard_path=HERE/'cpu-guard/guard.bin';guard=guard_path.read_bytes();assert sha(guard_path)==guard_report['guard_sha256']
 assert sha(store/'tree/bin/qc5-native-media')==guard_report['producer_sha256']
 assert len(guard)<4096;guard_at=0x80882000-load;data[guard_at:guard_at+4096]=guard+bytes(4096-len(guard))
 data=data[:0x80884000-load] # Retire the final raw-SD diagnostic routine.
 (OUT/'expanded.bin').write_bytes(data);stream=bytearray(lzma.compress(data,format=lzma.FORMAT_ALONE,filters=[{'id':lzma.FILTER_LZMA1,'dict_size':1<<22}]))
 # Pre-boot decoder uses the finite size, not FORMAT_ALONE's unknown marker.
 struct.pack_into('<Q',stream,5,len(data));assert lzma.decompress(stream,format=lzma.FORMAT_ALONE)==data;(OUT/'kernel.lzma').write_bytes(stream)
 src=HERE/'kernel-decoder/decompress_unlzma.original.c';mm=HERE/'kernel-decoder/mm.original.h'
 text=src.read_text().replace('#include <linux/decompress/mm.h>','#include "mm.original.h"').replace('#include <linux/compiler.h>','/* Standalone adapter requires no compiler attributes. */')
 # Keep a corrupt match inside the declared output, even before hash check.
 needle='\twr->buffer[wr->buffer_pos++] = wr->previous_byte = byte;';assert text.count(needle)==1;text=text.replace(needle,'\tif (get_pos(wr) >= wr->header->dst_size) return -1;\n'+needle)
 (OUT/'decompress_unlzma.adapted.c').write_text(text);(OUT/'mm.original.h').write_bytes(mm.read_bytes())
 value=2166136261
 for v in data:value=((value^v)*16777619)&0xffffffff
 (OUT/'image.generated.h').write_text(f'#define QC5_COMPRESSED_BYTES {len(stream)}u\n#define QC5_EXPANDED_BYTES {len(data)}u\n#define QC5_EXPANDED_FNV 0x{value:08x}u\n')
 for name in ['decoder.c','decoder-entry.S','decoder-start.c']:(OUT/name).write_bytes((HERE/name).read_bytes())
 (OUT/'image.S').write_text('.section .image,"a",@progbits\n.balign 16\n.globl __image_begin,__image_end\n__image_begin:\n.incbin "kernel.lzma"\n__image_end:\n')
 linker=OUT/'link.ld';linker.write_text('ENTRY(_start)\nPHDRS { image PT_LOAD FLAGS(7); }\nSECTIONS { . = 0x80a00000; .text : { *(.text.entry) *(.text*) } :image .rodata : { *(.rodata*) } :image .data : { *(.data*) *(.sdata*) *(.got*) } :image .image : { *(.image) } :image . = ALIGN(16); __bss_start = .; .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(16); . += 16384; __stack_top = .; } :image . = ALIGN(16); __bss_end = .; /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags*) *(.reginfo*) *(.pdr*) } }\n')
 # Use the same explicit GCC MIPS toolchain as the kernel; no Linux driver
 # defaults may re-enable abicalls/PIE in this freestanding loader.
 lab=runpy.run_path(str(ROOT/'video-gc1084/check-frame-qemu.py'));remote=subprocess.check_output(lab['SSH']+['mktemp -d /tmp/qc5-flash-decoder-XXXXXXXX'],text=True,timeout=20).strip();elf=OUT/'self-decoder.elf'
 compile_names=['decoder.c','decoder-start.c','decoder-entry.S','image.S','link.ld','image.generated.h','decompress_unlzma.adapted.c','mm.original.h','kernel.lzma']
 subprocess.run(lab['SCP']+[str(OUT/n) for n in compile_names]+['builder@127.0.0.1:'+remote+'/'],check=True,timeout=30)
 flags=['mipsel-linux-gnu-gcc','-Os','-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-mno-abicalls','-G0','-nostdlib','-static','-march=mips32','-msoft-float','-Wl,-T,link.ld,-e,_start,--build-id=none','decoder.c','decoder-start.c','decoder-entry.S','image.S','-o','self-decoder.elf']
 subprocess.run(lab['SSH']+['cd '+remote+' && '+' '.join(flags)],check=True,timeout=60);subprocess.run(lab['SCP']+['builder@127.0.0.1:'+remote+'/self-decoder.elf',str(elf)],check=True,timeout=20)
 segments=runpy.run_path(str(ROOT/'wifi-ram/build-image.py'))['PKG']['get_loads'](elf.read_bytes());entry,loads=segments;assert entry==0x80a00000 and len(loads)==1
 address,body,memsize=loads[0];assert address==entry and address+memsize<0x80d00000
 header=bytearray(struct.pack('>7I4B32s',0x27051956,0,0,len(body),address,entry,zlib.crc32(body),5,5,2,0,b'QC5 native7.3.1 flash'))
 struct.pack_into('>I',header,4,zlib.crc32(header));image=bytes(header)+body;assert len(image)<=0x200000,(len(image),0x200000)
 out=OUT/'QC5-R4D40E-FLASH-uImage.DISABLED';out.write_bytes(image)
 result=dict(compiled=True,filename=out.name,sha256=sha(out),bytes=len(image),stock_boot_read_bytes=0x200000,load=hex(address),entry=hex(entry),loader_memory_end=hex(address+memsize),expanded_kernel_end=hex(load+len(data)),kernel_sha256=b['kernel_sha256'],expanded_sha256=sha(OUT/'expanded.bin'),initramfs_bytes=len(archive),diagnostic_raw_SD_boot_checkpoints_removed=True,CPU_guard_matches_packaged_media=True,CPU_guard_marker_PC=guard_report['CPU_request_marker_PC'],raw_SD_diagnostic_syscall_dispatch_removed=True,no_kernel_profile_change=True,no_overlap_with_source_or_expanded_kernel=True,original_BOOT_and_environment_not_changed=True,real_U_Boot_boot_and_cache_handoff_test_pending=True,not_staged=True,armed=False,camera_accessed=False,flash_written=False,installable=False,
  sources={str(p):sha(p) for p in [Path(__file__),HERE/'cpu-guard/build.json',guard_path,store/'build.json',original/'build.json',original/b['filename'],src,mm,*[OUT/n for n in ['decoder.c','decoder-start.c','decoder-entry.S','image.S','link.ld','image.generated.h','decompress_unlzma.adapted.c','mm.original.h','kernel.lzma','expanded.bin','self-decoder.elf']],out]})
 (OUT/'build.json').write_text(json.dumps(result,indent=2)+'\n');print({k:v for k,v in result.items() if k!='sources'})
if __name__=='__main__':main()
