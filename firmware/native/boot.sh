#!/bin/sh
# Native gateway, no vendor restart and no CGI/preload video/PTZ bridge.
set -eu
unset LD_PRELOAD
HACK=${QC5_FIRMWARE_DIR:-/system/system/qc5}
SD=${QC5_SD_ROOT:-/mnt/sda0}
MOUNTS=${QC5_MOUNTS:-/proc/mounts}
BLOCK=${QC5_BLOCK_ROOT:-/sys/class/block}
QC5_CONFIG="$HACK/defaults/config.txt"
QC5_CONFIG_DEFAULT="$QC5_CONFIG"
export QC5_CONFIG QC5_CONFIG_DEFAULT
. "$HACK/lib/common.sh"
umask 077
"$BB" mkdir -p "$RUN"
"$BB" mkdir "$RUN/native-boot.lock" 2>/dev/null || exit 0
trap '"$BB" rmdir "$RUN/native-boot.lock" 2>/dev/null || true' 0
# Vendor clients are disabled in the application; Internet filtering belongs
# to the user's router. Starting the local UI must not require camera routing rules.
n=0
sd_mounted() { "$BB" awk -v p="$SD" '$2==p && $3=="vfat"{found=1} END{exit !found}' "$MOUNTS"; }
while ! sd_mounted && [ -e "$BLOCK/mmcblk0" ] && [ "$n" -lt 60 ];do sleep 2;n=$((n+1));done
if sd_mounted;then
 [ ! -e "$SD/QC5-SAFE-MODE" ] || exit 0
 if [ -f "$SD/config.txt" ];then
 [ "$("$BB" wc -c < "$SD/config.txt")" -le 8192 ] || exit 1
 QC5_CONFIG="$SD/config.txt"
 fi
fi
CONFIG="$QC5_CONFIG"
[ "$(cfg HACK_ENABLE)" = 1 ] || exit 0
MODE=$(cfg BOOT_MODE)
case "$MODE" in stock) exit 0;; full|provision|ui|'') :;; *) exit 1;;esac
# Keep maintenance available even if the WebUI/gateway cannot start.
export QC5_CONFIG QC5_SD_ROOT="$SD"
# USB Wi-Fi is initialized by the native application in parallel with this
# script. Do not lose SSH permanently just because its MAC is not ready yet.
NET=${QC5_NET_ROOT:-/sys/class/net}
network_identity_ready() {
 [ -r "$NET/wlan0/address" ] || return 1
 "$BB" awk -F: 'NF!=6{bad=1;next} {for(i=1;i<=6;i++)if($i!~/^[0-9a-fA-F][0-9a-fA-F]$/)bad=1;if(tolower($0)=="00:00:00:00:00:00"||tolower($0)=="ff:ff:ff:ff:ff:ff")bad=1;seen++} END{exit bad||seen!=1}' "$NET/wlan0/address"
}
n=0
while ! network_identity_ready && [ "$n" -lt 30 ];do sleep 2;n=$((n+1));done
network_identity_ready || echo QC5_WLAN_IDENTITY_NOT_READY >&2
if [ "$(cfg SSH_ENABLE)" = 1 ];then
 /bin/sh "$HACK/ssh.sh" start || echo QC5_SSH_START_FAILED >&2
fi
if [ "$(cfg NTP_ENABLE)" != 0 ] && [ -f "$HACK/ntp.sh" ];then
 (trap '' HUP;exec /bin/sh "$HACK/ntp.sh" --run) </dev/null >"$RUN/ntp.log" 2>&1 &
fi
PORT=$(cfg WEB_PORT);PORT=${PORT:-80};positive "$PORT" 65535 || exit 1
for bin in qc5-gateway;do
 A=$("$BB" md5sum "$HACK/bin/$bin");E=$("$BB" cat "$HACK/$bin.md5")
 [ "${A%% *}" = "$E" ] || exit 1
done
if [ -f "$RUN/gateway.pid" ];then
 P=$("$BB" cat "$RUN/gateway.pid")
 case "$P" in ''|*[!0-9]*) exit 1;;esac
 if kill -0 "$P" 2>/dev/null;then
  C=$("$BB" tr '\000' ' ' < "/proc/$P/cmdline")
  case "$C" in "$HACK/bin/qc5-gateway "*) exit 0;; *) exit 1;;esac
 fi
fi
WIFI_FLAG=
[ "$MODE" = ui ] || WIFI_FLAG='--provision'
EXPORT_FLAG=
STORE_BIN=${QC5_STORE_BIN:-/qc5/bin/qc5-store}
EXPORT_BIN=${QC5_RECORD_WORKER_BIN:-/qc5/bin/qc5-record-worker}
export_ready=1
for helper in qc5-store qc5-record-worker;do
 case "$helper" in qc5-store) binary=$STORE_BIN;; *) binary=$EXPORT_BIN;;esac
 if [ ! -x "$binary" ] || [ ! -f "$HACK/$helper.md5" ];then export_ready=0;break;fi
 A=$("$BB" md5sum "$binary");E=$("$BB" cat "$HACK/$helper.md5")
 if [ "${A%% *}" != "$E" ];then export_ready=0;break;fi
done
[ "$export_ready" = 0 ] || EXPORT_FLAG=--record-exports
RTSP_FLAG=
if [ "$(cfg RTSP_ENABLE)" != 0 ];then
 RTSP_PORT=$(cfg RTSP_PORT);RTSP_PORT=${RTSP_PORT:-8554}
 if positive "$RTSP_PORT" 65535 && [ "$RTSP_PORT" != "$PORT" ];then RTSP_FLAG="--rtsp-port $RTSP_PORT";else echo QC5_RTSP_PORT_INVALID >&2;fi
fi
(trap '' HUP;exec "$HACK/bin/qc5-gateway" --config "$CONFIG" --sd "$SD" --run-dir "$RUN" --webroot "$HACK/www" --port "$PORT" $WIFI_FLAG $EXPORT_FLAG $RTSP_FLAG --record-worker-bin "$EXPORT_BIN" --record-store-bin "$STORE_BIN") </dev/null >"$RUN/gateway.log" 2>&1 &
echo "$!" > "$RUN/gateway.pid"
echo QC5_NATIVE_GATEWAY_STARTED
