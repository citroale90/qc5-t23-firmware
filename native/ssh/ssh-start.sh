#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Preserve the flash identity; only a verified empty flash permits provisioning.
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
 load_status=$?
 if [ "$load_status" -ne 2 ];then
  # A damaged/unavailable device is different from an unprovisioned device.
  # Keep an already valid identity within this boot, never generate/import one.
  if [ -s "$key" ] && /bin/dropbearmulti dropbearkey -y -f "$key" >/dev/null 2>&1;then
   source=ram-existing
   printf 'QC5_SSH_IDENTITY flash_load_failed=%s existing_RAM_identity=1\n' "$load_status" >&2
  else
   printf 'QC5_SSH_IDENTITY flash_load_failed=%s identity_generation_refused=1\n' "$load_status" >&2
   exit "$load_status"
  fi
 else
  # Removable, unbound legacy SD keys must not clone another camera's identity.
  if [ ! -s "$key" ] || ! /bin/dropbearmulti dropbearkey -y -f "$key" >/dev/null 2>&1;then
   "$B" rm -f "$key"
   /bin/dropbearmulti dropbearkey -t ed25519 -f "$key" > /run/qc5-ssh/keygen.log 2>&1
  fi
  if ! /bin/qc5-ssh-key store;then
   # Do not let a retry mistake this unpersisted key for a verified identity.
   "$B" rm -f "$key" /run/qc5-ssh/identity-source
   printf 'QC5_SSH_IDENTITY initial_persistence_failed=1 server_start_refused=1\n' >&2
   exit 67
  fi
  source=flash
 fi
fi
printf '%s\n' "$source" > /run/qc5-ssh/identity-source
printf 'QC5_SSH_IDENTITY selected=%s\n' "$source" >&2
[ "$prepare" = 0 ] || exit 0
exec /bin/dropbearmulti dropbear -F -B -p 22 -r "$key" -P /run/qc5-ssh/server.pid
