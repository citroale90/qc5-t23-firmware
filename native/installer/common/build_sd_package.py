#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble an extract-to-SD factory kit; never access a camera or raw device."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,zipfile

TRIGGER='system.bin;sh${IFS}${PWD}mnt${PWD}sda0${PWD}qc5-run;#'
sha=lambda b:hashlib.sha256(b).hexdigest()

def render(text,values):
    for name,value in values.items():text=text.replace('@'+name+'@',value)
    if re.search(r'@[A-Z_]+@',text):raise ValueError('Unresolved installer template value')
    return text.encode()

def assemble(base,prepared,out,release,validation,checks=None):
    src=Path(__file__).resolve().parent
    base=base.resolve(strict=True);prepared=prepared.resolve(strict=True);out=out.resolve()
    if out.exists():raise ValueError('Output must be a new directory')
    listing=json.loads((base/'PACCHETTO.json').read_text())
    rows=[]
    for name,row in listing['files'].items():
        relative=Path(name)
        if relative.is_absolute() or any(p in ['.','..'] for p in relative.parts):raise ValueError('Unsafe package path')
        p=base/relative
        if p.is_symlink() or not p.is_file():raise ValueError('Non-regular package input')
        body=p.read_bytes()
        if len(body)!=row['bytes'] or sha(body)!=row['sha256']:raise ValueError('Damaged input: '+name)
        rows.append((relative,body))
    build=json.loads((prepared/'build.json').read_text())
    payload=(base/'qc5-installer/native.img').read_bytes()
    if len(payload)!=0x750000 or sha(payload)!=build['native_image_sha256'] or not build['production_preparer_readonly']:
        raise ValueError('Preparer and native payload do not match')
    for name in ['prepare','env-template.ko']:
        if sha((prepared/name).read_bytes())!=build['files'][name]:raise ValueError('Damaged compiled preparer')
    proof_checks=json.loads(checks.read_text()) if checks else None
    if proof_checks is not None and (not proof_checks.get('passed') or
            proof_checks.get('shipped_preparer_sha256')!=build['files']['prepare'] or
            proof_checks.get('native_payload_sha256')!=sha(payload)):
        raise ValueError('Installer tests do not match shipped binaries')
    out.mkdir(parents=True)
    for name,body in rows:
        p=out/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
    kit=out/'qc5-installer'
    for name in ['prepare','env-template.ko']:shutil.copyfile(prepared/name,kit/name)
    shutil.copyfile(src/'config.txt.example',kit/'config.txt.example')
    (kit/'preparer-build.json').write_text(json.dumps(build,indent=2)+'\n')
    pins=json.loads((src/'factory-entry-pins.json').read_text())
    values=dict(pins,BUSYBOX_SHA=sha((kit/'busybox').read_bytes()),
                PREPARE_SHA=sha((kit/'prepare').read_bytes()),
                ENV_HELPER_SHA=sha((kit/'env-template.ko').read_bytes()),
                LOADER_SHA=sha((kit/'Q73BOOT.IMG').read_bytes()))
    (kit/'run.sh').write_bytes(render((src/'run.sh.in').read_text(),values))
    entry=render((src/'qc5-run.in').read_text(),{
        'BUSYBOX_MD5':hashlib.md5((kit/'busybox').read_bytes()).hexdigest(),
        'RUN_MD5':hashlib.md5((kit/'run.sh').read_bytes()).hexdigest()})
    (out/'qc5-run').write_bytes(entry)
    (kit/'AZIONE.txt').write_bytes(b'INSTALLA\n');(out/TRIGGER).write_bytes(b'')
    for name in ['prepara_sd.py','aggiorna_qc5.py','ripristina_sd.py','LEGGIMI.txt','AVVIO-RAPIDO.txt']:
        shutil.copyfile(src/name,out/name)
    shutil.copyfile(src/'RIPRISTINO.txt',kit/'RIPRISTINO.txt')
    tag=release.replace('QC5-7.3.1-','',1).lower()
    notice=(out/'NOTICE.txt').read_text()
    notice+='\n'+release+' regenerates the read-only factory preparer and script integrity checks for its shipped payload. Corresponding source: https://github.com/citroale90/qc5-t23-firmware/tree/'+tag+' .\n'
    (out/'NOTICE.txt').write_text(notice)
    if validation:
        proof=json.loads(validation.read_text());(out/'VERIFICA-HARDWARE.json').write_text(json.dumps(proof,indent=2)+'\n')
    else:proof=json.loads((out/'VERIFICA-HARDWARE.json').read_text())
    if proof_checks:(out/'VERIFICA-INSTALLATORE.json').write_text(json.dumps(proof_checks,indent=2)+'\n')
    (out/'QUALIFICA.json').write_text(json.dumps(dict(release=release,
        native_image_sha256=sha(payload),runtime_unchanged_from_R47_RC1=sha(payload)=="25a600b2be79152db7ac460e18f194183b27648af92684d00aa995dd3c86bffc",
        factory_extract_to_root_ready=True,PC_script_required_for_factory_install=False,
        factory_preparer_payload_pin_generated_from_payload=True,
        script_pins_generated_from_packaged_files=True,actual_MIPS_preparer_checked=bool(proof_checks),physical_qualification=proof),indent=2)+'\n')
    manifest=json.loads((kit/'manifest.json').read_text())
    manifest.update(release=release,target_sha256=sha(payload),cold_entry_sha256=sha(entry),
        factory_extract_to_root_ready=True,PC_script_required_for_factory_install=False,
        native_update_requires_own_backup_and_preparation_tool=True)
    manifest['files']={p.name:dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
        for p in sorted(kit.iterdir()) if p.is_file() and p.name not in ['manifest.json','AZIONE.txt']}
    (kit/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    paths=[p for p in sorted(out.rglob('*')) if p.is_file() and p.name not in ['PACCHETTO.json','CHECKSUMS.sha256']]
    records={str(p.relative_to(out)):dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in paths}
    (out/'PACCHETTO.json').write_text(json.dumps(dict(release=release,files=records),indent=2)+'\n')
    paths=[p for p in sorted(out.rglob('*')) if p.is_file() and p.name!='CHECKSUMS.sha256']
    (out/'CHECKSUMS.sha256').write_text(''.join(sha(p.read_bytes())+'  '+str(p.relative_to(out))+'\n' for p in paths))
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        paths=[p for p in sorted(out.rglob('*')) if p.is_file()]
        # Publish the cold-entry trigger last. ZIP contains no enclosing folder
        # or config.txt that could overwrite the owner's saved credentials.
        paths.sort(key=lambda p:p.name==TRIGGER)
        for p in paths:
            relative=str(p.relative_to(out))
            if p.is_symlink() or any(x in relative for x in ['__MACOSX','._','PRIVATE']):raise ValueError('Unexpected archive input')
            info=zipfile.ZipInfo(relative,date_time=(2026,11,15,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
            z.writestr(info,p.read_bytes())
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:raise ValueError('ZIP verification failed')
    return dict(archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive.read_bytes()),
                native_image_sha256=sha(payload),preparer_sha256=values['PREPARE_SHA'],
                run_script_sha256=sha((kit/'run.sh').read_bytes()),cold_entry_sha256=sha(entry),
                factory_extract_to_root_ready=True,camera_accessed=False,flash_written=False)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['base-kit','prepared','output']:p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--release',default='QC5-7.3.1-R48-SD');p.add_argument('--validation',type=Path);p.add_argument('--checks',type=Path)
    a=p.parse_args();print(json.dumps(assemble(a.base_kit,a.prepared,a.output,a.release,a.validation,a.checks),indent=2))

if __name__=='__main__':main()
