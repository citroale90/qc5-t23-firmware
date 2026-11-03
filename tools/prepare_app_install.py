"""Prepare an UNARMED application installer capsule; never contact a device."""
from pathlib import Path
import hashlib,json,re,subprocess
import firmware_app_pair as fw
from elf_profile import require_mips_soft_float

PROJECT=Path(__file__).resolve().parents[1]
ENTRY=b'''#!/bin/sh
unset LD_PRELOAD
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/lib:/usr/lib
RUN_MD5=00000000000000000000000000000000
BB=/bin/busybox
P=/mnt/sda0/qc5-install
START=/tmp/qc5-app-installer-bootstrap
value=$("$BB" md5sum "$P/run.sh") || exit 1
[ "${value%% *}" = "$RUN_MD5" ] || { echo QC5_INSTALL_SCRIPT_HASH_FAILED >&2;exit 1; }
[ ! -e "$START" ] && [ ! -L "$START" ] || exit 1
"$BB" mkdir -m 700 "$START" || exit 1
"$BB" cp "$P/run.sh" "$START/run.sh" || exit 1
value=$("$BB" md5sum "$START/run.sh") || exit 1
[ "${value%% *}" = "$RUN_MD5" ] || { echo QC5_INSTALL_RAM_SCRIPT_HASH_FAILED >&2;exit 1; }
if [ -f "$P/APPROVE-RESTORE" ];then
 exec /bin/sh "$START/run.sh" --restore
fi
exec /bin/sh "$START/run.sh" --apply
'''
DRY_ENTRY=b'''#!/bin/sh
unset LD_PRELOAD
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/lib:/usr/lib
RUN_MD5=00000000000000000000000000000000
BB=/bin/busybox
P=/mnt/sda0/qc5-install
START=/tmp/qc5-app-installer-bootstrap
value=$("$BB" md5sum "$P/run.sh") || exit 1
[ "${value%% *}" = "$RUN_MD5" ] || exit 1
[ ! -e "$START" ] && [ ! -L "$START" ] || exit 1
"$BB" mkdir -m 700 "$START" || exit 1
"$BB" cp "$P/run.sh" "$START/run.sh" || exit 1
value=$("$BB" md5sum "$START/run.sh") || exit 1
[ "${value%% *}" = "$RUN_MD5" ] || exit 1
/bin/sh "$START/run.sh" --recovery-dry-run
status=$?
"$BB" rm "$START/run.sh"
"$BB" rmdir "$START"
[ "$status" = 0 ] || exit "$status"
# Optional laboratory return to an independently armed RAM-only trial. This
# hook never authorizes a firmware write and is absent on ordinary test cards.
if [ -f "$P/RESUME-TRIAL" ] && [ ! -L "$P/RESUME-TRIAL" ];then
 { IFS= read -r expected;IFS= read -r return_entry; } < "$P/RESUME-TRIAL" || exit 1
 value=$("$BB" md5sum /mnt/sda0/qc5-loop-trial/boot.sh) || exit 1
 [ "${value%% *}" = "$expected" ] || exit 1
 [ -f /mnt/sda0/qc5-loop-trial/ARMED ] && [ ! -L /mnt/sda0/qc5-loop-trial/ARMED ] || exit 1
 [ -f "$P/trial-return-entry" ] && [ ! -L "$P/trial-return-entry" ] || exit 1
 value=$("$BB" md5sum "$P/trial-return-entry") || exit 1
 [ "${value%% *}" = "$return_entry" ] || exit 1
 [ ! -e /mnt/sda0/qc5-run.after-recovery-test ] && [ ! -L /mnt/sda0/qc5-run.after-recovery-test ] || exit 1
 "$BB" cp "$P/trial-return-entry" /mnt/sda0/qc5-run.after-recovery-test || exit 1
 "$BB" mv /mnt/sda0/qc5-run.after-recovery-test /mnt/sda0/qc5-run || exit 1
 "$BB" rm "$P/RESUME-TRIAL"
 exec /bin/sh /mnt/sda0/qc5-loop-trial/boot.sh --run
fi
exit 0
'''
def md5(data):return hashlib.md5(data).hexdigest()
def sha(data):return hashlib.sha256(data).hexdigest()

def entry_for(script,dry_run=False):
    data=script.encode()if isinstance(script,str)else script
    template=DRY_ENTRY if dry_run else ENTRY
    return template.replace(b'RUN_MD5='+b'0'*32,b'RUN_MD5='+md5(data).encode())

def render(forward,rollback,verifier):
    checked=fw.check(forward);returned=fw.check(rollback)
    if checked['kind']!='modified'or returned['kind']!='stock':raise ValueError('An exact forward/stock-return pair is required')
    for source,target in [('source_system_sha256','system_sha256'),('source_www_sha256','www_sha256')]:
        if returned[source]!=checked[target]:raise ValueError('Return package belongs to a different candidate')
    require_mips_soft_float(verifier)
    if b'--target-device'not in verifier:raise ValueError('Full post-write verification is required')
    system=forward[fw.HEADER_SIZE:fw.HEADER_SIZE+fw.SYSTEM_SIZE];www=forward[fw.HEADER_SIZE+fw.SYSTEM_SIZE:]
    original_system=rollback[fw.HEADER_SIZE:fw.HEADER_SIZE+fw.SYSTEM_SIZE];original_www=rollback[fw.HEADER_SIZE+fw.SYSTEM_SIZE:]
    values={'FORWARD_SHA':sha(forward),'RETURN_SHA':sha(rollback),'FORWARD_MD5':md5(forward),'RETURN_MD5':md5(rollback),
        'VERIFIER_MD5':md5(verifier),'ROOT_ENTRY_MD5':md5(ENTRY),'DRY_ENTRY_MD5':md5(DRY_ENTRY),'SYSTEM_MD5':md5(system),'WWW_MD5':md5(www),
        'ORIGINAL_SYSTEM_MD5':md5(original_system),'ORIGINAL_WWW_MD5':md5(original_www)}
    script=(PROJECT/'firmware/native/app-install.sh').read_text()
    for name,value in values.items():script=script.replace('@'+name+'@',value)
    if re.search(r'@[A-Z_]+@',script):raise ValueError('Unresolved installer input')
    subprocess.run(['/bin/sh','-n'],input=script.encode(),check=True)
    return script,values

def prepare(forward,rollback,verifier,output,configuration=None):
    script,values=render(forward,rollback,verifier)
    files={'run.sh':script.encode(),'firmware.qc5pkg':forward,'original.qc5pkg':rollback,'qc5-verify':verifier,
        'qc5-run.EXAMPLE':entry_for(script),'qc5-recovery-test.EXAMPLE':entry_for(script,True)}
    if configuration is not None:
        text=configuration.decode('utf-8')
        for line in text.splitlines():
            if '='in line and not line.lstrip().startswith('#'):
                key,value=line.split('=',1)
                if key.strip()in('SSID','PWD','SMB_PASSWORD','FTP_PASSWORD')and value.strip().strip('\"\''):
                    raise ValueError('Only an empty credential template may be distributed')
        if not re.search(r'^SSH_ENABLE=1$',text,re.M):raise ValueError('Root SSH must remain enabled')
        files['config.txt.EXAMPLE']=configuration
    output=Path(output);output.mkdir(mode=0o700)
    for name,data in files.items():
        (output/name).write_bytes(data);(output/name).chmod(0o700 if name in('run.sh','qc5-verify')else 0o600)
    report=dict(armed=False,approval_marker_created=False,root_entry_installed=False,factory_trigger_created=False,
        default_action='read_only_check',requires_original_cold_sd_loader=True,refuses_running_sdk=True,
        backup_before_first_erase='Complete 8 MiB device read to SD, compared with a second full raw read',
        writes_only=['mtd3 system','mtd4 www'],root_kernel_boot_private_preserved=True,
        all_inputs_copied_and_verified_in_ram_before_erase=True,full_target_device_readback_required=True,
        root_entry_checks_installer_before_execution=True,installer_script_copied_and_rechecked_in_ram=True,
        recovery_dry_run_supported=True,recovery_dry_run_requires_original_cold_loader=True,
        recovery_dry_run_unmounts_applications_without_writing_them=True,
        recovery_dry_run_restores_original_mounts=True,recovery_dry_run_no_unlock_erase_or_flashcp=True,
        root_entry_digest_mode='RUN_MD5 field normalized to zero; all other entry bytes checked',
        factory_busybox_flashcp_used=True,install_requires_hardware_validation=True,
        hashes={name:dict(bytes=len(data),sha256=sha(data))for name,data in files.items()},values=values,
        camera_contacted=False,flash_written=False)
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
