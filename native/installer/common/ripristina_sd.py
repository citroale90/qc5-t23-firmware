#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline SD preparation/recovery. No network, raw disk or flash access."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,os,re,stat,struct,zlib,datetime
TRIGGER='system.bin;sh${IFS}${PWD}mnt${PWD}sda0${PWD}qc5-run;#'
CONTROL=['Q73ARM.BIN','Q73LOG.BIN','Q73BOOT.IMG','QC5UPD.BIN','QC5FULL.BIN']
SIZE=0x750200

def sha(b):return hashlib.sha256(b).digest()
def path(sd,relative):
 rel=PurePosixPath(relative)
 if rel.is_absolute() or '..' in rel.parts:raise ValueError('Percorso fuori dalla SD')
 p=sd
 for part in rel.parts:
  p=p/part
  if p.is_symlink():raise ValueError('Collegamento non consentito: '+relative)
 return p
def read(sd,relative,size=None):
 p=path(sd,relative);s=p.stat()
 if not stat.S_ISREG(s.st_mode) or s.st_nlink!=1 or (size is not None and s.st_size!=size):raise ValueError('File non regolare o dimensione errata: '+relative)
 return p.read_bytes()
def write(sd,relative,data):
 p=path(sd,relative);tmp=path(sd,relative+'.NEW')
 if p.exists() and not p.is_file():raise ValueError('Destinazione non regolare')
 if tmp.exists():
  read(sd,relative+'.NEW');tmp.unlink()
 fd=os.open(str(tmp),os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600)
 try:
  at=0
  while at<len(data):
   n=os.write(fd,data[at:])
   if n<=0:raise OSError('Scrittura incompleta')
   at+=n
  os.fsync(fd)
 finally:os.close(fd)
 if tmp.read_bytes()!=data:raise ValueError('Rilettura SD diversa')
 os.replace(str(tmp),str(p))
def sync():getattr(os,'sync',lambda:None)()
def disable_trigger(sd):
 t=path(sd,TRIGGER)
 if t.exists():
  read(sd,TRIGGER,0);t.unlink()
def archive_controls(sd):
 existing=[n for n in CONTROL if path(sd,n).exists()]
 for n in existing:read(sd,n)
 if existing:
  base=path(sd,'qc5-backups');base.mkdir(exist_ok=True)
  dest=base/('sd-state-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));dest.mkdir()
  for n in existing:os.replace(str(sd/n),str(dest/n))
 return existing

def unit_pair(sd,relative):
 rel=PurePosixPath(relative.removeprefix('/mnt/sda0/'))
 if len(rel.parts)!=2 or rel.parts[0]!='qc5-backups' or not re.fullmatch(r'unit-[A-Za-z0-9]{6}',rel.parts[1]):raise ValueError('Cartella del backup non valida')
 u=str(rel);a=read(sd,u+'/original-1.bin',0x800000);b=read(sd,u+'/original-2.bin',0x800000);back=read(sd,u+'/QC5FULL.BIN',SIZE)
 if a!=b:raise ValueError('Le due letture del backup non coincidono')
 if back[:8]!=b'QC5R73\0\0' or struct.unpack_from('<6I',back,8)!=(512,0x750000,0x40000,0x790000,0x8000,1) or back[512:]!=a[0x40000:0x790000] or sha(back[:480])!=back[480:512] or sha(back[512:])!=back[192:224]:raise ValueError('Capsula di recupero non valida')
 for at,n,off in [(0,0x3c000,96),(0x790000,0x30000,128),(0x7c0000,0x40000,160)]:
  if sha(a[at:at+n])!=back[off:off+32]:raise ValueError('Capsula di altra camera')
 # Each backup retains its original loader and manifest across later SD upgrades.
 local=path(sd,u+'/Q73BOOT.IMG').exists()
 kit=u if local else 'qc5-installer'
 m=json.loads(read(sd,kit+'/manifest.json'));loader=read(sd,kit+'/Q73BOOT.IMG')
 if sha(loader).hex()!=m['files']['Q73BOOT.IMG']['sha256'] or not re.fullmatch('[0-9a-f]{64}',m['target_sha256']):raise ValueError('Caricatore o manifesto non valido')
 return u,a,back,m,loader

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sd',required=True);p.add_argument('--backup');g=p.add_mutually_exclusive_group(required=True)
 for name in ['elenca','stato','prepara-ripristino','prepara-installazione','nuova-camera','disattiva']:g.add_argument('--'+name,action='store_true')
 a=p.parse_args();sd=Path(a.sd).resolve(strict=True)
 if not sd.is_dir():raise ValueError('Serve una cartella SD montata, non un disco grezzo')
 if a.elenca:
  base=path(sd,'qc5-backups')
  for u in sorted(base.glob('unit-*')) if base.exists() else []:
   data=read(sd,str(u.relative_to(sd))+'/original-1.bin',0x800000);ids=sorted(set(re.findall(rb'VE[0-9]{7}[A-Z]{4}',data[0x7c0000:])))
   print(u.relative_to(sd),'/'.join(x.decode() for x in ids) or 'ID non rilevato','SHA256',sha(data).hex()[:16])
  return
 if a.stato:
  b=read(sd,'Q73LOG.BIN',512)
  if b[:8]!=b'QT4LOG\r\n' or zlib.crc32(b[:508])!=struct.unpack_from('<I',b,508)[0]:raise ValueError('Registro non valido')
  version,seq,mode,phase,address,rc=struct.unpack_from('<5Ii',b,8);print(json.dumps(dict(versione=version,sequenza=seq,azione=chr(mode) if mode else '-',fase=phase,indirizzo=hex(address),esito=rc,completato=phase==8 and rc>0),indent=2));return
 if a.disattiva:
  disable_trigger(sd);arm=path(sd,'Q73ARM.BIN')
  if arm.exists():read(sd,'Q73ARM.BIN',8);arm.unlink()
  sync();print('Ingresso installatore e recupero disattivati.');return
 if a.nuova_camera:
  m=json.loads(read(sd,'qc5-installer/manifest.json'))
  if sha(read(sd,'qc5-run')).hex()!=m['cold_entry_sha256']:raise ValueError('Ingresso SD non integro')
  for n,h in m['files'].items():
   if sha(read(sd,'qc5-installer/'+n)).hex()!=h['sha256']:raise ValueError('File del pacchetto non integro: '+n)
  archive_controls(sd);write(sd,'qc5-installer/AZIONE.txt',b'INSTALLA\n');write(sd,TRIGGER,b'');sync();print('SD pronta per una gemella originale. Backup precedenti conservati.');return
 relative=a.backup or read(sd,'qc5-current-unit.txt').decode().strip();u,snapshot,back,m,loader=unit_pair(sd,relative)
 if a.prepara_installazione:
  checked=read(sd,'Q73LOG.BIN',512)
  if checked[:8]!=b'QT4LOG\r\n' or zlib.crc32(checked[:508])!=struct.unpack_from('<I',checked,508)[0] or struct.unpack_from('<I',checked,16)[0]!=ord('C') or struct.unpack_from('<I',checked,20)[0]!=8 or struct.unpack_from('<i',checked,28)[0]!=3:raise ValueError('Manca un controllo C/8/3 concluso')
  up=read(sd,u+'/QC5UPD.BIN',SIZE)
  if up[:8]!=b'QC5U73\0\0' or up[32:192]!=back[32:192] or up[224:256]!=back[192:224] or sha(up[:480])!=up[480:512] or sha(up[512:])!=up[192:224] or up[192:224].hex()!=m['target_sha256'] or checked[32:64]!=up[192:224]:raise ValueError('Aggiornamento non abbinato al controllo')
  current=json.loads(read(sd,'qc5-installer/manifest.json'))
  if current['target_sha256']!=m['target_sha256'] or sha(read(sd,'qc5-run')).hex()!=m['cold_entry_sha256'] or read(sd,'Q73ARM.BIN',8)!=bytes(8):raise ValueError('Ingresso SD o versione non abbinati')
  write(sd,'qc5-installer/AZIONE.txt',b'INSTALLA\n');write(sd,TRIGGER,b'');path(sd,'Q73ARM.BIN').unlink();sync();print('INSTALLAZIONE PREPARATA: accendi con Reset per circa 3 secondi; attendi 15 minuti.');return
 # Validate destinations first. A rejected backup must not alter any marker.
 for n in ['Q73ARM.BIN','Q73BOOT.IMG','QC5FULL.BIN','Q73LOG.BIN']:
  if path(sd,n).exists():read(sd,n)
 if path(sd,TRIGGER).exists():read(sd,TRIGGER,0)
 log=bytearray(512);log[:8]=b'QT4LOG\r\n';struct.pack_into('<I',log,8,1);log[32:64]=bytes.fromhex(m['target_sha256']);struct.pack_into('<I',log,508,zlib.crc32(log[:508]))
 disable_trigger(sd);write(sd,'Q73ARM.BIN',bytes(8));sync()
 for n,data in [('Q73BOOT.IMG',loader),('QC5FULL.BIN',back),('Q73LOG.BIN',log)]:write(sd,n,data)
 write(sd,'Q73ARM.BIN',b'QC5R73\r\n');sync();print('RIPRISTINO PRONTO per '+u+': accendi senza Reset; attendi 15 minuti. Dopo il ripristino togli la SD o disattivala prima della prossima accensione.')
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,KeyError,json.JSONDecodeError) as e:raise SystemExit('Operazione non completata: '+str(e))
