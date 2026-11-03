#!/bin/sh
# Application-only SD installer. Requires explicit per-package approval and
# the original cold SD loader; never invoke --apply on a running camera.
set -eu
unset LD_PRELOAD
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/lib:/usr/lib
umask 077
ulimit -c 0
cd /
BB=/bin/busybox
SD=/mnt/sda0
P=$SD/qc5-install
RUN=/tmp/qc5-app-install
PROC=/proc
ACTION=${1:---check}
CRITICAL=0
FLASH_LOCK=/usr/sbin/sfc_lock
FLASH_UNLOCKED=0
DRY_RUN=0
DRY_SYSTEM_UNMOUNTED=0
DRY_WWW_UNMOUNTED=0
DRY_RAM_CREATED=0
restore_dry_mounts() {
 if [ "$DRY_SYSTEM_UNMOUNTED" = 1 ];then
  "$BB" mount -t squashfs -o ro /dev/mtdblock3 /system/system || return 1
  DRY_SYSTEM_UNMOUNTED=0
 fi
 if [ "$DRY_WWW_UNMOUNTED" = 1 ];then
  "$BB" mount -t squashfs -o ro /dev/mtdblock4 /system/www || return 1
  DRY_WWW_UNMOUNTED=0
 fi
}
install_exit() {
 status=$?
 if [ "$DRY_RUN" = 1 ];then
  restore_dry_mounts || {
   trap - 0
   echo QC5_RECOVERY_DRY_RUN_MOUNT_RESTORE_FAILED >&2
   while :;do sleep 60;done
  }
  if [ "$DRY_RAM_CREATED" = 1 ];then
   for name in system.img www.img package qc5-verify;do
    if [ -e "$RUN/$name" ] || [ -L "$RUN/$name" ];then
     [ -f "$RUN/$name" ] && [ ! -L "$RUN/$name" ] || return 1
     "$BB" rm "$RUN/$name" || return 1
    fi
   done
   "$BB" rmdir "$RUN" || return 1
  fi
 fi
 if [ "$CRITICAL" = 1 ];then
  trap - 0
  echo QC5_INSTALL_CRITICAL_EXIT_BLOCKED >&2
  while :;do sleep 60;done
 fi
 return "$status"
}
trap install_exit 0
fail() {
 echo "QC5_INSTALL_FAILED_$1" >&2
 if [ "$CRITICAL" = 1 ];then
  # The stock loader reboots after its child exits. Remain here on a failed
  # transaction; its original watchdog worker is still running separately.
  while :;do sleep 60;done
 fi
 exit 1
}
trap 'fail INTERRUPTED' 1 2 3 15
case "$ACTION" in
 --check|--apply) MODE=install;;
 --recovery-check|--restore) MODE=restore;;
 --recovery-dry-run) MODE=restore;DRY_RUN=1;;
 *) exit 2;;
esac
[ "$#" -le 1 ] || exit 2
[ -d "$SD" ] && [ ! -L "$SD" ] && [ -d "$P" ] && [ ! -L "$P" ] || fail SD_PATH
CHECK=--device
PACKAGE=firmware.qc5pkg
EXPECTED=@FORWARD_SHA@
APPROVAL=APPROVE-INSTALL
if [ "$MODE" = restore ];then
 CHECK=--recovery-check;PACKAGE=original.qc5pkg;EXPECTED=@RETURN_SHA@;APPROVAL=APPROVE-RESTORE
fi
check_file() {
 [ -f "$P/$1" ] && [ ! -L "$P/$1" ] || fail FILE_TYPE
 value=$("$BB" md5sum "$P/$1") || fail FILE_READ
 [ "${value%% *}" = "$2" ] || fail FILE_HASH
}
check_file qc5-verify @VERIFIER_MD5@
check_file "$PACKAGE" "$(if [ "$MODE" = restore ];then echo @RETURN_MD5@;else echo @FORWARD_MD5@;fi)"
ALREADY_TARGET=0
if ! "$P/qc5-verify" "$CHECK" "$P/$PACKAGE";then
 "$P/qc5-verify" --target-device "$P/$PACKAGE" || fail PACKAGE_OR_PLATFORM
 ALREADY_TARGET=1
elif [ "$MODE" = restore ] && "$P/qc5-verify" --target-device "$P/$PACKAGE" >/dev/null 2>&1;then
 ALREADY_TARGET=1
fi
case "$ACTION" in --check|--recovery-check) echo QC5_INSTALL_CHECK_OK;exit 0;;esac
if [ "$DRY_RUN" = 0 ];then
 [ -f "$P/$APPROVAL" ] && [ ! -L "$P/$APPROVAL" ] || fail APPROVAL_MISSING
 IFS= read -r approved < "$P/$APPROVAL"
 [ "$approved" = "$EXPECTED" ] || fail APPROVAL_MISMATCH
 ENTRY_MD5=@ROOT_ENTRY_MD5@
else
 # This marker authorizes a read-only cold exercise, never a flash write.
 [ -f "$P/ARM-RECOVERY-TEST" ] && [ ! -L "$P/ARM-RECOVERY-TEST" ] || fail RECOVERY_TEST_NOT_ARMED
 IFS= read -r approved < "$P/ARM-RECOVERY-TEST"
 [ "$approved" = "$EXPECTED" ] || fail RECOVERY_TEST_PACKAGE_MISMATCH
 ENTRY_MD5=@DRY_ENTRY_MD5@
fi
# Normalize only the entry's bound script digest to break a checksum cycle.
# Every other entry byte, including commands and paths, remains covered.
value=$("$BB" awk '/^RUN_MD5=[0-9a-f]+$/{if(length($0)!=40)exit 1;$0="RUN_MD5=00000000000000000000000000000000";seen++}{print}END{if(seen!=1)exit 1}' "$SD/qc5-run" | "$BB" md5sum) || fail ENTRY_READ
[ "${value%% *}" = "$ENTRY_MD5" ] || fail ENTRY_HASH
[ -z "$("$BB" pidof vp_project systemcall qc5-gateway 2>/dev/null || true)" ] || fail SDK_RUNNING
ancestor=$PPID;found=0
for depth in 1 2 3 4 5 6;do
 case "$ancestor" in ''|*[!0-9]*) fail LOADER_PARENT;;esac
 [ "$ancestor" -gt 1 ] || fail LOADER_PARENT
 exe=$("$BB" readlink "$PROC/$ancestor/exe" 2>/dev/null || true)
 if [ "$exe" = /usr/sbin/tfUpgrade ];then
  value=$("$BB" md5sum "$PROC/$ancestor/exe") || fail LOADER_READ
  [ "${value%% *}" = 81f935926f2df9639c98a473cab23d9f ] || fail LOADER_HASH
  found=1;break
 fi
 parent=
 while read -r key value rest;do [ "$key" != PPid: ] || parent=$value;done < "$PROC/$ancestor/status"
 ancestor=$parent
done
[ "$found" = 1 ] || fail LOADER_PARENT
"$BB" awk -v p="$SD" '$2==p&&$3=="vfat"&&$4~/^rw,/{ok=1}END{exit !ok}' "$PROC/mounts" || fail SD_NOT_WRITABLE_FAT32
"$BB" df -Pk "$SD" | "$BB" awk 'NR==2{seen=1;if($4<32768)exit 1}END{if(!seen)exit 1}' || fail SD_SPACE
"$BB" awk '/^MemFree:/{f=$2}/^Buffers:/{b=$2}/^Cached:/{c=$2}/^Shmem:/{s=$2}END{if(c<s)c=s;exit f+b/2+(c-s)/2-768<6400}' "$PROC/meminfo" || fail RAM_BUDGET
IFS= read -r boot < "$PROC/sys/kernel/random/boot_id"
case "$boot" in ''|*[!0-9a-f-]*) fail BOOT_ID;;esac
BACKUP=$SD/qc5-backup-before-$MODE-$boot
if [ "$DRY_RUN" = 1 ];then BACKUP=$SD/qc5-backup-recovery-test-$boot;fi
[ ! -e "$BACKUP" ] && [ ! -L "$BACKUP" ] && [ ! -e "$RUN" ] && [ ! -L "$RUN" ] || fail EXISTING_STATE
"$BB" mkdir -m 700 "$BACKUP" || fail BACKUP_DIRECTORY
if [ "$DRY_RUN" = 1 ];then
 exec >> "$BACKUP/recovery-test.log" 2>&1
 printf 'RECOVERY_TEST_BOOT %s\nORIGINAL_LOADER_AND_NO_SDK_VERIFIED\n' "$boot"
 # Simulate unavailable application filesystems using mounts only. All
 # running tools, libraries and the SD loader must come from original ROOT.
 # The physical SYSTEM/WWW bytes remain untouched; EXIT remounts the originals.
 "$BB" rm "$P/ARM-RECOVERY-TEST"
 TRIGGER='system.bin;sh${IFS}${PWD}mnt${PWD}sda0${PWD}qc5-run;#'
 if [ -f "$SD/$TRIGGER" ] && [ ! -L "$SD/$TRIGGER" ];then "$BB" rm "$SD/$TRIGGER";fi
 for mountpoint in /system/system /system/www;do
  mounted=$("$BB" awk -v p="$mountpoint" '$2==p{if($3!="squashfs"||$4!~/^ro(,|$)/)exit 2;found=1}END{print found+0}' "$PROC/mounts") || fail RECOVERY_TEST_MOUNT_PROFILE
  case "$mounted" in 0|1) :;; *) fail RECOVERY_TEST_MOUNT_PROFILE;;esac
  if [ "$mounted" = 1 ];then
   "$BB" umount "$mountpoint" || fail RECOVERY_TEST_MOUNT_BUSY
   if [ "$mountpoint" = /system/system ];then DRY_SYSTEM_UNMOUNTED=1;else DRY_WWW_UNMOUNTED=1;fi
  fi
 done
 [ ! -e /system/system/bin/vp_project ] && [ ! -e /system/www/index.html ] || fail RECOVERY_TEST_APPLICATION_FILES_VISIBLE
 echo QC5_RECOVERY_TEST_APPLICATION_FILESYSTEMS_UNAVAILABLE
fi
# Preserve this unit's whole original/current flash, including private NVS.
# dd's ONLY output is an SD file. The raw MTD alias is read twice, never opened
# as an output device. A failed backup prevents the first erase.
"$BB" dd if=/dev/mtd6 of="$BACKUP/flash-all.bin" bs=32768 count=256 || fail BACKUP_COPY
[ "$("$BB" wc -c < "$BACKUP/flash-all.bin")" -eq 8388608 ] || fail BACKUP_SIZE
a=$("$BB" md5sum "$BACKUP/flash-all.bin") || fail BACKUP_READBACK
b=$("$BB" md5sum /dev/mtd6) || fail BACKUP_SECOND_READ
[ "${a%% *}" = "${b%% *}" ] || fail BACKUP_MISMATCH
printf '%s\n' "${a%% *}" > "$BACKUP/flash-all.md5"
"$BB" sync
"$BB" mkdir -m 700 "$RUN" || fail RAM_DIRECTORY
if [ "$DRY_RUN" = 1 ];then DRY_RAM_CREATED=1;fi
"$BB" cp "$P/qc5-verify" "$RUN/qc5-verify" || fail RAM_VERIFIER
"$BB" chmod 700 "$RUN/qc5-verify"
value=$("$BB" md5sum "$RUN/qc5-verify");[ "${value%% *}" = @VERIFIER_MD5@ ] || fail RAM_VERIFIER_HASH
"$BB" cp "$P/$PACKAGE" "$RUN/package" || fail RAM_PACKAGE
value=$("$BB" md5sum "$RUN/package")
if [ "$MODE" = restore ];then expected_md5=@RETURN_MD5@;else expected_md5=@FORWARD_MD5@;fi
[ "${value%% *}" = "$expected_md5" ] || fail RAM_PACKAGE_HASH
RAM_CHECK=$CHECK
[ "$ALREADY_TARGET" = 0 ] || RAM_CHECK=--target-device
"$RUN/qc5-verify" "$RAM_CHECK" "$RUN/package" || fail RAM_PACKAGE_CHECK
"$BB" dd if="$RUN/package" of="$RUN/system.img" bs=512 skip=1 count=5312 || fail SYSTEM_EXTRACT
"$BB" dd if="$RUN/package" of="$RUN/www.img" bs=512 skip=5313 count=384 || fail WWW_EXTRACT
[ "$("$BB" wc -c < "$RUN/system.img")" -eq 2719744 ] || fail SYSTEM_SIZE
[ "$("$BB" wc -c < "$RUN/www.img")" -eq 196608 ] || fail WWW_SIZE
case "$MODE" in
 install) system_md5=@SYSTEM_MD5@;www_md5=@WWW_MD5@;;
 restore) system_md5=@ORIGINAL_SYSTEM_MD5@;www_md5=@ORIGINAL_WWW_MD5@;;
esac
value=$("$BB" md5sum "$RUN/system.img");[ "${value%% *}" = "$system_md5" ] || fail SYSTEM_BYTES
value=$("$BB" md5sum "$RUN/www.img");[ "${value%% *}" = "$www_md5" ] || fail WWW_BYTES
if [ "$DRY_RUN" = 1 ];then
 # Last read-only boundary. No unlock, erase, flashcp or write-target path can
 # be reached by this action. Verify ROOT/BOOT/kernel and stock APP bytes again
 # after the complete backup/RAM preparation, then leave normal mounts intact.
 "$RUN/qc5-verify" --recovery-check "$RUN/package" || fail RECOVERY_TEST_FINAL_BASE
 echo RECOVERY_TEST_BACKUP_AND_STOCK_RAM_INPUTS_VERIFIED
 restore_dry_mounts || fail RECOVERY_TEST_REMOUNT
 echo RECOVERY_TEST_ORIGINAL_APPLICATION_MOUNTS_RESTORED
 "$BB" rm "$RUN/system.img" "$RUN/www.img" "$RUN/package" "$RUN/qc5-verify"
 "$BB" rmdir "$RUN"
 DRY_RAM_CREATED=0
 "$BB" sync
 echo QC5_RECOVERY_DRY_RUN_COMPLETE_NO_FLASH_WRITE
 exit 0
fi
# Every input is now verified in RAM, independent of subsequent SD I/O.
# A failure from here must not fall back into an automatic stock-loader reboot.
value=$("$BB" md5sum "$FLASH_LOCK") || fail FLASH_LOCK_READ
[ "${value%% *}" = 3cfc73c60a0246c1f18fcb497ca9b456 ] || fail FLASH_LOCK_HASH
CRITICAL=1
printf 'BACKUP_VERIFIED\nINPUTS_VERIFIED\n' > "$BACKUP/install.log"
if [ "$ALREADY_TARGET" = 0 ];then
for mountpoint in /system/system /system/www;do
 mounted=$("$BB" awk -v p="$mountpoint" '$2==p{found=1}END{print found+0}' "$PROC/mounts") || fail MOUNT_LIST
 case "$mounted" in 0|1) :;; *) fail MOUNT_LIST;;esac
 if [ "$mounted" = 1 ];then
  "$BB" umount "$mountpoint" || fail APPLICATION_MOUNT_BUSY
 fi
done
# rcS protects the NOR before entering the SD loader. Use the immutable
# native utility explicitly; do not depend on flashcp implicitly unlocking.
"$FLASH_LOCK" unlock >> "$BACKUP/install.log" 2>&1 || fail FLASH_UNLOCK
FLASH_UNLOCKED=1
echo FLASH_WWW >> "$BACKUP/install.log";"$BB" sync
"$BB" flashcp -v "$RUN/www.img" /dev/mtd4 >> "$BACKUP/install.log" 2>&1 || fail WWW_WRITE
echo FLASH_SYSTEM >> "$BACKUP/install.log";"$BB" sync
"$BB" flashcp -v "$RUN/system.img" /dev/mtd3 >> "$BACKUP/install.log" 2>&1 || fail SYSTEM_WRITE
else
 echo ALREADY_TARGET_NO_FLASH_WRITE >> "$BACKUP/install.log"
fi
"$RUN/qc5-verify" --target-device "$RUN/package" >> "$BACKUP/install.log" 2>&1 || fail TARGET_READBACK
if [ "$FLASH_UNLOCKED" = 1 ];then
 "$FLASH_LOCK" lock >> "$BACKUP/install.log" 2>&1 || fail FLASH_RELOCK
 FLASH_UNLOCKED=0
fi
echo TARGET_FULLY_VERIFIED >> "$BACKUP/install.log"
"$BB" sync
# Consume only this package's explicit approval after full device readback.
"$BB" rm "$P/$APPROVAL"
TRIGGER='system.bin;sh${IFS}${PWD}mnt${PWD}sda0${PWD}qc5-run;#'
if [ -f "$SD/$TRIGGER" ] && [ ! -L "$SD/$TRIGGER" ];then "$BB" rm "$SD/$TRIGGER";fi
"$BB" sync
CRITICAL=0
echo QC5_INSTALL_COMPLETE
# The original loader controls the subsequent reboot. No bootloader, kernel,
# ROOT, private NVS or Wi-Fi configuration was replaced by this script.
exit 0
