#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy this kit to an existing FAT32/MBR card, without formatting/raw I/O."""
from pathlib import Path
import argparse,hashlib,json,runpy,shutil

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sd',required=True);a=p.parse_args();source=Path(__file__).resolve().parent;sd=Path(a.sd).resolve(strict=True)
 if not sd.is_dir() or sd==source:raise ValueError('Scegli la cartella della SD montata, diversa dal pacchetto')
 lib=runpy.run_path(str(source/'ripristina_sd.py'));read,write,path=lib['read'],lib['write'],lib['path'];m=json.loads(read(source,'PACCHETTO.json'));items=[]
 for n,row in m['files'].items():
  data=read(source,n,row['bytes'])
  if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Pacchetto danneggiato: '+n)
  if path(sd,n).exists() and not path(sd,n).is_file():raise ValueError('Destinazione non regolare: '+n)
  items.append((n,data))
 if shutil.disk_usage(sd).free<0x8000000+sum(len(b) for n,b in items):raise ValueError('Servono almeno 128 MiB liberi oltre al pacchetto')
 # Preserve every previous backup and archive only the installer's root controls.
 lib['archive_controls'](sd);lib['disable_trigger'](sd)
 path(sd,'qc5-installer').mkdir(exist_ok=True)
 for n,data in items:
  if n==lib['TRIGGER']:continue
  write(sd,n,data)
 write(sd,'PACCHETTO.json',read(source,'PACCHETTO.json'))
 if not path(sd,'config.txt').exists():write(sd,'config.txt',read(source,'qc5-installer/config.txt.example'))
 write(sd,'qc5-installer/AZIONE.txt',b'INSTALLA\n')
 write(sd,lib['TRIGGER'],b'');lib['sync']()
 print('SD R44 PRONTA. Backup e config.txt preesistenti conservati. Espelli normalmente la SD; inseriscila nella gemella originale spenta; alimenta con Reset per circa 3 secondi e attendi almeno 15 minuti.')
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,KeyError,json.JSONDecodeError) as e:raise SystemExit('Preparazione non completata: '+str(e))
