#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Flash identity takes precedence over an optional/removable SD host key.
set -eu
case "$#:${1:-}" in 0:) prepare=0;; 1:--prepare-key) prepare=1;; *) exit 64;; esac
B=/bin/busybox-full
umask 077
"$B" mkdir -p /run/qc5-ssh /dev/pts /root /usr/bin
"$B" --install -s /usr/bin
export PATH=/usr/bin:/bin
if ! "$B" grep -q ' /dev/pts devpts ' /proc/mounts;then
 "$B" mount -t devpts -o mode=600,ptmxmode=600 devpts /dev/pts
fi
key=/run/qc5-ssh/hostkey
saved=/mnt/sda0/qc5-state/ssh-hostkey
source=flash
if /bin/qc5-ssh-key load;then
 if ! /bin/dropbearmulti dropbearkey -y -f "$key" >/dev/null 2>&1;then
  printf 'QC5_SSH_IDENTITY invalid_flash_key=1\n' >&2
  exit 70
 fi
 # No erase on normal boots. Repair only a missing/damaged second copy,
 # including a power interruption during the first provisioning.
 if ! /bin/qc5-ssh-key store;then printf 'QC5_SSH_IDENTITY replica_repair_failed=1\n' >&2;fi
else
 source=ram
 if [ ! -s "$key" ] || ! /bin/dropbearmulti dropbearkey -y -f "$key" >/dev/null 2>&1;then
  "$B" rm -f "$key"
  if [ -f "$saved" ] && [ ! -L "$saved" ];then
   size=$("$B" stat -c '%s' "$saved")
   if [ "$size" = 83 ];then
    "$B" cp "$saved" "$key"
    if /bin/dropbearmulti dropbearkey -y -f "$key" >/dev/null 2>&1;then source=sd;else "$B" rm -f "$key";fi
   fi
  fi
 fi
 if [ ! -s "$key" ];then
  /bin/dropbearmulti dropbearkey -t ed25519 -f "$key" > /run/qc5-ssh/keygen.log 2>&1
  source=generated
 fi
 if /bin/qc5-ssh-key store;then source=flash;else printf 'QC5_SSH_IDENTITY persistence_failed=1 source=%s\n' "$source" >&2;fi
fi
printf '%s\n' "$source" > /run/qc5-ssh/identity-source
printf 'QC5_SSH_IDENTITY selected=%s\n' "$source" >&2
[ "$prepare" = 0 ] || exit 0
exec /bin/dropbearmulti dropbear -F -B -p 22 -r "$key" -P /run/qc5-ssh/server.pid
