#!/usr/bin/env python3
"""Standalone SD preparation; standard Python 3 only, no AI/network/NAS.

Release packaging replaces the three pinned manifest/package placeholders.
This program only prepares mounted FAT32 media. The camera performs hardware
and source-firmware verification before any erase at the next cold SD boot.
"""
from pathlib import Path
import argparse, datetime, hashlib, json, os, plistlib, shutil, subprocess, sys, uuid

MANIFEST_SHA='@MANIFEST_SHA@'
FORWARD_SHA='@FORWARD_SHA@'
RETURN_SHA='@RETURN_SHA@'
TRIGGER='system.bin;sh${IFS}${PWD}mnt${PWD}sda0${PWD}qc5-run;#'
TOKENS=('VP_OTA_','u-boot-with-spl','uImage','rootfs_64k_T23','VP_FLASH-','system.bin','jz_www')
FLAGS=('APPROVE-INSTALL','APPROVE-RESTORE','ARM-RECOVERY-TEST','RESUME-TRIAL')

def digest(data):return hashlib.sha256(data).hexdigest()
def plain(path):return path.is_file() and not path.is_symlink()

def source_files(folder):
    folder=Path(folder)
    if not plain(folder/'manifest.json'):raise ValueError('Manifest mancante o non regolare')
    raw=(folder/'manifest.json').read_bytes()
    if digest(raw)!=MANIFEST_SHA:raise ValueError('Manifest diverso dal pacchetto previsto')
    manifest=json.loads(raw);values=manifest['values']
    if values['FORWARD_SHA']!=FORWARD_SHA or values['RETURN_SHA']!=RETURN_SHA:
        raise ValueError('Coppia installazione/ripristino diversa')
    data={'manifest.json':raw}
    for name,info in manifest['hashes'].items():
        if name!=Path(name).name or name in FLAGS:raise ValueError('Nome non valido nel manifest')
        path=folder/name
        if not plain(path):raise ValueError('File mancante o non regolare: '+name)
        value=path.read_bytes()
        if len(value)!=info['bytes'] or digest(value)!=info['sha256']:
            raise ValueError('Integrita non valida: '+name)
        data[name]=value
    for name in ('firmware.qc5pkg','original.qc5pkg','qc5-verify','run.sh','qc5-run.EXAMPLE','config.txt.EXAMPLE'):
        if name not in data:raise ValueError('Pacchetto incompleto')
    return data

def validate_mount(sd):
    if sd.is_symlink() or not sd.is_dir() or not os.path.ismount(sd):
        raise ValueError('Indicare la radice di una SD FAT32 montata, non una normale cartella')
    if sys.platform=='darwin':
        r=subprocess.run(['/usr/sbin/diskutil','info','-plist',str(sd)],capture_output=True,timeout=10,check=True)
        info=plistlib.loads(r.stdout)
        if info.get('FilesystemType')!='msdos' or 'FAT32' not in info.get('FilesystemName','').replace(' ','').upper():
            raise ValueError('La SD deve essere FAT32 (MS-DOS FAT32)')
    elif sys.platform.startswith('linux'):
        found=False
        for line in Path('/proc/mounts').read_text().splitlines():
            fields=line.split();point=fields[1].replace('\\040',' ').replace('\\011','\t').replace('\\134','\\')
            if point==str(sd) and fields[2]=='vfat' and 'rw' in fields[3].split(','):found=True
        if not found:raise ValueError('La SD deve essere montata in lettura/scrittura come vfat')
    else:raise ValueError('Preparazione supportata su macOS e Linux')
    if shutil.disk_usage(sd).free<32*1024*1024:raise ValueError('Servono almeno 32 MiB liberi sulla SD')

def audit(sd,files=None,recovery=False):
    # A known interrupted transaction may be replaced by stock recovery. Only
    # exact files from this release qualify; unrelated update signals are refused.
    owned=False;entry=sd/'qc5-run';trigger=sd/TRIGGER;base=sd/'qc5-install'
    if recovery and files and plain(entry) and entry.read_bytes()==files['qc5-run.EXAMPLE'] and plain(base/'run.sh') and (base/'run.sh').read_bytes()==files['run.sh']:
        owned=True
        if trigger.exists() and (not plain(trigger) or trigger.stat().st_size!=0):owned=False
        for flag,expected in [('APPROVE-INSTALL',FORWARD_SHA),('APPROVE-RESTORE',RETURN_SHA)]:
            f=base/flag
            if f.exists() and (not plain(f) or f.read_bytes()!=(expected+'\n').encode()):owned=False
    for path in sd.iterdir():
        if path.is_symlink():raise ValueError('Collegamento nella radice SD: '+path.name)
        if not path.is_dir() and any(token in path.name for token in TOKENS):
            if owned and path.name==TRIGGER:continue
            raise ValueError('Vecchio segnale di aggiornamento da esaminare prima: '+path.name)
    for directory in ('qc5-install','qc5-loop-trial','qc5-cow-trial','qc5-ram-trial','qc5-trial'):
        base=sd/directory
        if base.is_symlink():raise ValueError('Cartella collegata non ammessa: '+directory)
        for flag in FLAGS+('ARMED',):
            if (base/flag).exists() or (base/flag).is_symlink():
                if owned and directory=='qc5-install' and flag in('APPROVE-INSTALL','APPROVE-RESTORE'):continue
                raise ValueError('Esiste gia un’attivazione: '+directory+'/'+flag)
    config=sd/'config.txt'
    if config.exists() and (not plain(config) or config.stat().st_size>8192):
        raise ValueError('config.txt non regolare o troppo grande')
    if entry.exists() and not plain(entry):raise ValueError('qc5-run non regolare')
    return owned and trigger.exists()

def write(path,data,executable=False):
    with path.open('xb')as f:f.write(data);f.flush();os.fsync(f.fileno())
    path.chmod(0o700 if executable else 0o600)

def prepare(sd,files,action):
    sd=Path(sd).absolute()
    validate_mount(sd);old_trigger=audit(sd,files,action=='restore')
    if action=='check':return dict(checked=True,armed=False)
    unique=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    backup=sd/('QC5-preparazione-backup-'+unique);backup.mkdir(mode=0o700)
    stage=sd/('.qc5-capsule-new-'+unique);stage.mkdir(mode=0o700)
    entrytmp=sd/('.qc5-entry-new-'+unique);dest=sd/'qc5-install'
    old_entry=(sd/'qc5-run').read_bytes()if (sd/'qc5-run').exists()else None
    old_config=(sd/'config.txt').read_bytes()if (sd/'config.txt').exists()else None
    old_moved=new_moved=entry_replaced=new_config=approval_created=trigger_created=False
    approval='APPROVE-RESTORE'if action=='restore'else'APPROVE-INSTALL'
    try:
        if old_entry is not None:write(backup/'qc5-run',old_entry)
        if old_config is not None:write(backup/'config.txt.PRIVATE',old_config)
        if old_trigger:
            write(backup/'previous-activation.txt',b'Known interrupted installation trigger removed before recovery preparation.\n')
            (sd/TRIGGER).unlink();os.sync()
        for name,data in files.items():
            write(stage/name,data,name in('run.sh','qc5-verify'))
            if (stage/name).read_bytes()!=data:raise ValueError('Rilettura SD diversa: '+name)
        write(entrytmp,files['qc5-run.EXAMPLE'])
        if old_config is None:
            new_config=True;write(sd/'config.txt',files['config.txt.EXAMPLE'])
        if dest.exists():os.replace(dest,backup/'qc5-install');old_moved=True
        os.replace(stage,dest);new_moved=True
        os.replace(entrytmp,sd/'qc5-run');entry_replaced=True
        write(dest/approval,((RETURN_SHA if action=='restore'else FORWARD_SHA)+'\n').encode());approval_created=True
        os.sync()
        with (sd/TRIGGER).open('xb')as f:
            trigger_created=True;f.flush();os.fsync(f.fileno())
        os.sync()
        if (sd/'qc5-run').read_bytes()!=files['qc5-run.EXAMPLE']:raise ValueError('Ingresso SD non integro')
        return dict(checked=True,armed=True,action=action,backup_directory=str(backup),wifi_config_preserved=old_config is not None)
    except BaseException:
        # A preparation error must not leave an intentional boot trigger.
        if trigger_created:(sd/TRIGGER).unlink()
        if approval_created:(dest/approval).unlink()
        if entry_replaced:
            if old_entry is None:(sd/'qc5-run').unlink()
            else:(sd/'qc5-run').write_bytes(old_entry)
        if new_moved:shutil.rmtree(dest)
        if old_moved:os.replace(backup/'qc5-install',dest)
        if new_config:(sd/'config.txt').unlink()
        os.sync();raise
    finally:
        if entrytmp.exists():entrytmp.unlink()
        if stage.exists():shutil.rmtree(stage)

def main():
    p=argparse.ArgumentParser(description='Prepara una SD per QC5 T23 V82 senza IA, NAS o Internet. Non formatta la scheda.')
    p.add_argument('--sd',required=True,type=Path,help='Radice della scheda FAT32 montata')
    group=p.add_mutually_exclusive_group();group.add_argument('--installa',action='store_true');group.add_argument('--ripristina',action='store_true')
    a=p.parse_args();action='restore'if a.ripristina else'install'if a.installa else'check'
    os.umask(0o077)
    try:
        files=source_files(Path(__file__).resolve().parent/'qc5-install')
        result=prepare(a.sd,files,action)
        print(json.dumps(result,indent=2,ensure_ascii=False))
        if result['armed']:
            print('SD pronta per '+('RIPRISTINO ORIGINALE'if action=='restore'else'INSTALLAZIONE')+'.')
            print('Espellere normalmente la SD. Inserirla nella camera SPENTA; accendere con Reset premuto e rilasciarlo dopo circa 3 secondi.')
            print('Lasciare alimentata almeno 5 minuti, senza QR o altri Reset. Non interrompere la programmazione.')
        else:print('Solo controllo completato: installazione non attivata.')
    except Exception as error:
        print('Preparazione interrotta: '+str(error),file=sys.stderr);return 1
    return 0

if __name__=='__main__':raise SystemExit(main())
