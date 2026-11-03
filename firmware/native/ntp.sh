#!/bin/sh
# Use the stock BusyBox client with ONE local peer and no Internet fallback.
set -eu
unset LD_PRELOAD
HACK=${QC5_FIRMWARE_DIR:-/system/system/qc5}
. "$HACK/lib/common.sh"
CONFIG=${QC5_CONFIG:-$HACK/defaults/config.txt}
ROUTE=${QC5_ROUTE_FILE:-/proc/net/route}
ACTION=${1:---run}
case "$ACTION" in --run|--check) :;; *) exit 2;;esac
[ "$(cfg NTP_ENABLE)" != 0 ] || exit 0
local_peer() {
 printf '%s\n' "$1" | "$BB" awk -F. '
 NF!=4{exit 1}
 {for(i=1;i<=4;i++)if($i!~/^[0-9]+$/||length($i)>3||$i+0>255||(length($i)>1&&substr($i,1,1)=="0"))exit 1;
  if(!($1==10||($1==172&&$2>=16&&$2<=31)||($1==192&&$2==168)||($1==169&&$2==254)))exit 1;
  if($4==0||$4==255)exit 1;ok=1}
 END{if(!ok)exit 1}'
}
gateway() {
 "$BB" awk '
 function byte(h, a,b){a=index("0123456789ABCDEF",toupper(substr(h,1,1)))-1;b=index("0123456789ABCDEF",toupper(substr(h,2,1)))-1;if(a<0||b<0)return -1;return a*16+b}
 $1=="wlan0"&&$2=="00000000"&&length($3)==8&&$3!~/[^0-9a-fA-F]/{
  printf "%d.%d.%d.%d\n",byte(substr($3,7,2)),byte(substr($3,5,2)),byte(substr($3,3,2)),byte(substr($3,1,2));exit}' "$ROUTE"
}
configured=$(cfg NTP_SERVER)
case "$configured" in auto|'') :;; *) local_peer "$configured" || { echo QC5_NTP_INVALID_LOCAL_SERVER >&2;exit 1; };;esac
if [ "$ACTION" = --run ];then
 "$BB" mkdir -p "$RUN"
 # RAM-only lock: native boot may be invoked twice, but never spawn two peers.
 "$BB" mkdir "$RUN/ntp.lock" 2>/dev/null || exit 0
 trap '"$BB" rmdir "$RUN/ntp.lock" 2>/dev/null || true' 0
fi
while :;do
 server=$configured
 case "$server" in auto|'') server=$(gateway);;esac
 if [ -n "$server" ];then
  local_peer "$server" || { echo QC5_NTP_INVALID_LOCAL_SERVER >&2;exit 1; }
  break
 fi
 [ "$ACTION" != --check ] || exit 1
 # First provisioning can take minutes. Wait without DNS or network probes;
 # the shell is replaced by ntpd as soon as station routing is ready.
 "$BB" sleep 10
done
if [ "$ACTION" = --check ];then printf 'QC5_NTP_PEER %s\n' "$server";exit 0;fi
printf '%s\n' "$server" > "$RUN/ntp.peer"
printf '%s\n' "$$" > "$RUN/ntp.pid"
printf 'QC5_NTP_PEER %s\n' "$server"
# Exec closes inherited native device/socket descriptors. No extra BusyBox,
# server mode, public peer, DHCP-supplied peer, or permanent shell is used.
exec "$HACK/bin/qc5-exec-clean" "$BB" ntpd -n -p "$server"
