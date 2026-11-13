#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the cold read-only preparer; never access a camera or SD."""
from pathlib import Path
import argparse,subprocess,shutil,hashlib,json

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True);p.add_argument('--zig',default='zig');p.add_argument('--mips-cc',default='mipsel-linux-gnu-gcc');a=p.parse_args();src=Path(__file__).resolve().parent;out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False);tree=out/'source';tree.mkdir()
 for f in src.iterdir():
  if f.suffix in ['.c','.h']:shutil.copyfile(f,tree/f.name)
 flags=['-Os','-mips32','-msoft-float','-mno-abicalls','-fno-pic','-fno-pie','-G0','-fno-stack-protector','-fno-asynchronous-unwind-tables','-DQC5_ENV_ERASE','-c',str(tree/'env-sector.c'),'-o',str(out/'env-template.ko')]
 subprocess.run([a.mips_cc,*flags],check=True);raw=(out/'env-template.ko').read_bytes();needle=b'QC5ENVUNITPREFIX'
 if raw.count(needle)!=1:raise ValueError('ENV prefix not unique')
 (tree/'env-helper.generated.h').write_text('#define QENV_HELPER_SHA_BYTES {'+','.join(str(x) for x in hashlib.sha256(raw).digest())+'}\n#define QENV_HELPER_BYTES '+str(len(raw))+'u\n#define QENV_HELPER_PREFIX_OFFSET '+str(raw.index(needle))+'u\n')
 subprocess.run([a.zig,'cc','-Os','-static','-s','-target','mipsel-linux-musl','-mcpu=mips32','-DQC5_PREPARE_ONLY',str(tree/'flash-linux.c'),str(tree/'full-recovery.c'),'-o',str(out/'prepare')],check=True)
 if b'/dev/mtd6\0' in (out/'prepare').read_bytes():raise ValueError('Writable MTD command path found')
 report={'compiled':True,'camera_accessed':False,'SD_accessed':False,'production_preparer_readonly':True,'files':{n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ['prepare','env-template.ko']}};(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
