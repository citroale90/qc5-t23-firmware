#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Native production startup from verified read-only flash, no vendor init.
set -eu
exec > /dev/kmsg 2>&1
printf '4\n' > /proc/sys/kernel/printk
B=/bin/busybox-full
finish() {
 status=$?
 printf 'QC5_NATIVE_FLASH_SERVICES_EXIT status=%s waiting_for_watchdog=1\n' "$status"
 while :;do "$B" sleep 1;done
}
trap finish EXIT
umask 077
"$B" mkdir -p /run/wpa /run/qc5-native-network /mnt/sda0 /tmp /root /dev/pts
"$B" chmod 700 /run /run/wpa /run/qc5-native-network
"$B" ln -sf /bin/busybox-full /bin/sh
"$B" ip link set dev lo up
# Optional storage must never prevent local network/UI startup.
start_storage() {
if ! /bin/check-msc-clock > /run/clock;then
 printf "QC5_SD_STORAGE_SKIPPED reason=clock_unavailable no_card_writes=1\n"
 return 0
fi
for module in qc5-msc-clock-ro mmc_core mmc_block nls_cp437 nls_iso8859-1 nls_utf8 fat vfat;do
 if ! "$B" insmod "/modules/$module.ko";then printf "QC5_SD_STORAGE_SKIPPED reason=module_unavailable no_card_writes=1\n";return 0;fi
done
if ! "$B" insmod /modules/qc5-mmc-recording.ko;then printf "QC5_SD_STORAGE_SKIPPED reason=controller_unavailable no_card_writes=1\n";return 0;fi
# SD is optional for startup. Its absence must still permit the local AP/UI.
for tick in 1 2 3;do [ ! -b /dev/mmcblk0 ] || break;"$B" sleep 1;done
if [ -b /dev/mmcblk0 ] && /bin/qc5-record-volume --cid-params > /run/sd-cid;then
 read -r params < /run/sd-cid
 if ! "$B" rmmod qc5_mmc_recording;then return 0;fi
 if ! "$B" insmod /modules/qc5-mmc-recording.ko recording_writes=1 "$params";then return 0;fi
 for tick in 1 2 3;do [ ! -b /dev/mmcblk0p1 ] || break;"$B" sleep 1;done
 if [ -b /dev/mmcblk0p1 ];then
  if /bin/qc5-record-volume --preflight;then
   "$B" mount -t vfat -o noexec,nodev,nosuid,noatime,usefree,iocharset=iso8859-1,umask=0077 /dev/mmcblk0p1 /mnt/sda0 || :
  else
   "$B" mount -t vfat -o ro,noexec,nodev,nosuid,usefree,iocharset=iso8859-1,umask=0077 /dev/mmcblk0p1 /mnt/sda0 || :
  fi
 fi
fi
}
"$B" insmod /modules/qc5-wifi-config.ko config_writes=1
if ! "$B" insmod /modules/qc5-ssh-key.ko key_writes=1;then printf "QC5_SSH_IDENTITY flash_module_unavailable=1\n" >&2;fi
start_storage
"$B" insmod /modules/qc5-peripheral.ko
"$B" insmod /modules/qc5-illumination.ko
"$B" insmod /modules/aic_load_fw.ko aic_fw_path=/lib/firmware
/bin/qc5-factory-mac --read > /run/factory-mac
read -r factory_mac < /run/factory-mac
"$B" insmod /modules/aic8800_fdrv.ko "factory_mac=$factory_mac"
wlan=
for tick in $("$B" seq 1 30);do
 for iface in /sys/class/net/*;do [ ! -d "$iface/phy80211" ] || { wlan=${iface##*/};break; };done
 [ -z "$wlan" ] || break
 "$B" sleep 1
done
[ -n "$wlan" ]
printf '%s\n' "$wlan" > /run/live-net-iface
/bin/qc5-factory-mac --verify "$wlan" "$factory_mac"
/bin/qc5-ssh-start > /dev/kmsg 2>&1 &
trap - EXIT
exec /bin/qc5-service-supervisor "$wlan"
