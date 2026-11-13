#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare only regular files on this unit's SD. Never access a raw disk."""
from pathlib import Path
import os,sys,hashlib,json,struct,zlib,stat
if len(sys.argv)!=3 or sys.argv[1] not in ['controlla','installa','ripristina','disattiva']:
 sys.exit('Uso: python3 gestisci_sd.py controlla|installa|ripristina|disattiva PERCORSO_SD')
mode=sys.argv[1];root=Path(sys.argv[2]).resolve();
if not root.is_dir():sys.exit('La destinazione deve essere una cartella SD montata, mai un disco grezzo')
m=json.loads((Path(__file__).resolve().parent/'MANIFEST.json').read_text())
if not isinstance(m.get('files'),dict) or set(m['files'])!={'QC5UPD.BIN','QC5FULL.BIN','Q73BOOT.IMG'}:sys.exit('Elenco file del manifesto non valido')
def read(name):
 p=root/name
 if p.is_symlink() or not p.is_file():sys.exit('File mancante/non regolare: '+name)
 return p.read_bytes()
def write(name,data):
 p=root/name
 if p.is_symlink():sys.exit('Collegamento non consentito: '+name)
 flags=os.O_WRONLY|os.O_CREAT|getattr(os,'O_NOFOLLOW',0)
 fd=os.open(str(p),flags,0o600)
 try:
  if not stat.S_ISREG(os.fstat(fd).st_mode):sys.exit('File non regolare')
  done=0
  while done<len(data):
   written=os.pwrite(fd,data[done:],done)
   if written<=0:raise OSError('Scrittura SD incompleta')
   done+=written
  os.ftruncate(fd,len(data));os.fsync(fd)
 finally:os.close(fd)
 if read(name)!=data:sys.exit('Verifica rilettura fallita')
for name,wanted in m['files'].items():
 if hashlib.sha256(read(name)).hexdigest()!=wanted:sys.exit('Checksum errato: '+name)
if mode=='controlla':print('PACCHETTO VERIFICATO per '+m['unit']);sys.exit(0)
if mode=='disattiva':
 p=root/'Q73ARM.BIN'
 if p.exists():os.replace(str(p),str(root/'Q73ARM.OFF'))
 print('Caricatore SD disattivato: il prossimo avvio usa la flash.');sys.exit(0)
log=bytearray(512);log[:8]=b'QT4LOG\r\n';struct.pack_into('<I',log,8,1);log[32:64]=bytes.fromhex(m['payload_sha256']);struct.pack_into('<I',log,508,zlib.crc32(log[:508]))
write('Q73LOG.BIN',log);write('Q73ARM.BIN',b'QC5U73\r\n' if mode=='installa' else b'QC5R73\r\n')
print('SD PRONTA per '+mode.upper()+' su '+m['unit']+'. Espellila normalmente prima di inserirla nella camera spenta.')
