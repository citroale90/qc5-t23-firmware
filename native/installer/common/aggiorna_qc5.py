#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a unit-bound SD update for a camera already running native QC5.

PC filesystem operations only. The camera installer validates the actual
BOOT/WWW/PARAM identity again before writing. Factory cameras use prepara_sd.py.
"""
from pathlib import Path
import argparse,hashlib,json,runpy,struct,zlib

def sha(data):return hashlib.sha256(data).digest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sd',required=True)
    parser.add_argument('--backup')
    args=parser.parse_args()
    source=Path(__file__).resolve().parent
    sd=Path(args.sd).resolve(strict=True)
    if not sd.is_dir() or sd==source:
        raise ValueError('Indica la SD montata, distinta dalla cartella del pacchetto')
    lib=runpy.run_path(str(source/'ripristina_sd.py'))
    read,write,path=lib['read'],lib['write'],lib['path']
    listing=json.loads(read(source,'PACCHETTO.json'))
    files={}
    for name in ['qc5-installer/manifest.json','qc5-installer/native.img','qc5-installer/Q73BOOT.IMG']:
        row=listing['files'][name]
        data=read(source,name,row['bytes'])
        if sha(data).hex()!=row['sha256']:raise ValueError('Pacchetto danneggiato: '+name)
        files[name]=data
    manifest=json.loads(files['qc5-installer/manifest.json'])
    payload=files['qc5-installer/native.img'];loader=files['qc5-installer/Q73BOOT.IMG']
    if len(payload)!=0x750000 or sha(payload).hex()!=manifest['target_sha256']:
        raise ValueError('Immagine firmware non abbinata al pacchetto')
    if sha(loader).hex()!=manifest['files']['Q73BOOT.IMG']['sha256']:
        raise ValueError('Caricatore non abbinato al pacchetto')
    relative=args.backup or read(sd,'qc5-current-unit.txt').decode().strip()
    unit,snapshot,back,old_manifest,old_loader=lib['unit_pair'](sd,relative)
    # The existing native boot policy must accept the new CRC-balanced loader
    # without changing ENV. Original backups retain their original loader.
    if loader[:64]!=old_loader[:64] or len(loader)!=len(old_loader):
        raise ValueError('Il nuovo caricatore richiede una diversa configurazione di avvio')
    header=bytearray(back[:512]);header[:8]=b'QC5U73\0\0'
    struct.pack_into('<I',header,28,2)
    header[192:224]=sha(payload);header[224:256]=back[192:224]
    header[480:512]=sha(header[:480]);forward=bytes(header)+payload
    journal=bytearray(512);journal[:8]=b'QT4LOG\r\n';struct.pack_into('<I',journal,8,1)
    journal[32:64]=sha(payload);struct.pack_into('<I',journal,508,zlib.crc32(journal[:508]))
    # Validate all existing destinations before changing any control file.
    for name in ['Q73ARM.BIN','Q73BOOT.IMG','QC5UPD.BIN','QC5FULL.BIN','Q73LOG.BIN']:
        if path(sd,name).exists():read(sd,name)
    lib['archive_controls'](sd);lib['disable_trigger'](sd)
    write(sd,'Q73ARM.BIN',bytes(8));lib['sync']()
    for name,data in [('QC5UPD.BIN',forward),('QC5FULL.BIN',back),
                      ('Q73BOOT.IMG',loader),('Q73LOG.BIN',journal)]:
        write(sd,name,data)
    write(sd,'qc5-current-unit.txt',(unit+'\n').encode())
    write(sd,'Q73ARM.BIN',b'QC5U73\r\n');lib['sync']()
    print('AGGIORNAMENTO SD PRONTO per '+unit+'. Solo per camera con QC5 già installato. '
          'Espelli la SD, inseriscila nella SUA camera spenta e accendi SENZA Reset. '
          'Attendi almeno 15 minuti. Il backup originale resta disponibile per il ripristino.')

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,KeyError,json.JSONDecodeError) as error:
        raise SystemExit('Aggiornamento non preparato: '+str(error))
