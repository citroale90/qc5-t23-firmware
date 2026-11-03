#!/bin/sh
BB=${QC5_BUSYBOX:-/bin/busybox}
RUN=${QC5_RUN:-/tmp/qc5-hack}
CONFIG=${QC5_CONFIG:-${HACK%/*}/config.txt}
PRESETS=${QC5_PERSIST:-$HACK}/presets
export PATH=/system/system/bin:/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/system/system/lib:/mnt/lib
unset LD_PRELOAD
cfg() {
    # Resident defaults precede SD overrides. An explicit empty value or zero
    # on the SD still wins; a missing service key must not disable maintenance.
    if [ -n "${QC5_CONFIG_DEFAULT:-}" ] && [ "$QC5_CONFIG_DEFAULT" != "$CONFIG" ];then
        LC_ALL=C "$BB" awk -v key="$1" -f "$HACK/lib/config.awk" "$QC5_CONFIG_DEFAULT" "$CONFIG" 2>/dev/null
    else
        LC_ALL=C "$BB" awk -v key="$1" -f "$HACK/lib/config.awk" "$CONFIG" 2>/dev/null
    fi
}
jstr() { printf '%s' "$1" | LC_ALL=C "$BB" awk -f "$HACK/lib/json.awk"; }
error() { printf '{"ok":false,"result":'; jstr "$1"; printf '}\n'; }
success() { printf '{"ok":true,"result":'; jstr "$1"; printf '}\n'; }
native() {
    # nc's connect timeout alone must not stall CGI or the supervisor when a
    # vendor driver blocks the adapter after accepting the connection.
    reply=$(printf '%s\n' "$1" | "$BB" timeout -t 5 "$BB" nc -w 3 127.0.0.1 "${QC5_BRIDGE_PORT:-18082}" 2>/dev/null) || reply=
    case "$reply" in '{"ok":'*) printf '%s\n' "$reply" ;; *) error 'Adattatore QC5 non attivo: avviare la prova hardware' ;; esac
}
positive() { case "$1" in ''|*[!0-9]*) return 1 ;; esac; [ "$1" -ge 1 ] && [ "$1" -le "$2" ]; }
wifi_config_ready() {
    # Validate first-connection data before allowing a restart without a LAN.
    # Values are data only and are never printed or evaluated by the shell.
    wc_ssid=$(cfg SSID) && wc_pwd=$(cfg PWD) &&
    wc_apply=$(cfg WIFI_APPLY) && wc_open=$(cfg WIFI_OPEN) || return 1
    [ "$wc_apply" != 0 ] || return 1
    wc_n=$(printf '%s' "$wc_ssid" | "$BB" wc -c)
    [ "$wc_n" -ge 1 ] && [ "$wc_n" -le 32 ] || return 1
    wc_n=$(printf '%s' "$wc_pwd" | "$BB" wc -c)
    if [ "$wc_n" -eq 0 ];then [ "$wc_open" = 1 ];else
        [ "$wc_n" -ge 8 ] && [ "$wc_n" -le 63 ]
    fi
}
