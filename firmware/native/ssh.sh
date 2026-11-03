#!/bin/sh
# Root SSH for this QC5 profile. RAM accounts; optional per-unit SD host key.
set -eu
umask 077
HACK=${QC5_FIRMWARE_DIR:-/system/system/qc5}
. "$HACK/lib/common.sh"
SD=${QC5_SD_ROOT:-/mnt/sda0}
ETC=${QC5_ETC:-/etc}
NET=${QC5_NET_ROOT:-/sys/class/net}
MOUNTS=${QC5_MOUNTS:-/proc/mounts}
STATE="$RUN/ssh"
BIN="$HACK/bin/dropbearmulti"
CLEAN="$HACK/bin/qc5-exec-clean"
fail() { echo "QC5_SSH: $1" >&2;exit 1; }
server_alive() {
    [ -f "$STATE/server.pid" ] || return 1
    PID=$("$BB" cat "$STATE/server.pid")
    case "$PID" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$PID" 2>/dev/null || return 1
    COMMAND=$("$BB" tr '\000' ' ' < "/proc/$PID/cmdline") || return 1
    case "$COMMAND" in *"$BIN"*dropbear*) return 0 ;; *) return 1 ;; esac
}
lock() {
    "$BB" mkdir -p "$STATE"
    "$BB" mkdir "$STATE/start.lock" || fail 'Operazione SSH gia in corso'
    trap '"$BB" rmdir "$STATE/start.lock"' 0
}
restore_account() {
    [ -f "$STATE/passwd-bound" ] || return 0
    # A stale marker must never unmount another service's account overlay.
    [ "$STATE/passwd" -ef "$ETC/passwd" ] || fail 'Overlay account diverso: ripristino annullato'
    "$BB" umount "$ETC/passwd"
    "$BB" rm -f "$STATE/passwd-bound"
}
prepare_hostkey() {
    KEYDIR="$SD/qc5/ssh/$DEVICE"
    PERSIST=0
    # A bare mount-point directory is not a card. Never put state there, and
    # never follow a card symlink into another part of the camera filesystem.
    if "$BB" awk -v p="$SD" '$2==p && $3=="vfat" {found=1} END{exit !found}' "$MOUNTS" &&
       [ ! -L "$SD/qc5" ] && [ ! -L "$SD/qc5/ssh" ] && [ ! -L "$KEYDIR" ];then
        PERSIST=1
    fi
    if [ "$PERSIST" = 1 ] && [ -f "$KEYDIR/ed25519.key" ] && [ ! -L "$KEYDIR/ed25519.key" ];then
        if "$BB" cp "$KEYDIR/ed25519.key" "$STATE/new.key" &&
           "$BIN" dropbearkey -y -f "$STATE/new.key" >/dev/null 2>"$STATE/keycheck.log";then
            "$BB" mv "$STATE/new.key" "$STATE/hostkey"
            echo sd > "$STATE/hostkey-source"
        else
            PERSIST=0
            "$BB" rm -f "$STATE/new.key"
            echo 'QC5_SSH: chiave SD non valida o non leggibile; uso una chiave in RAM' >&2
        fi
    fi
    if [ ! -s "$STATE/hostkey" ];then
        "$BB" rm -f "$STATE/new.key"
        "$BIN" dropbearkey -t ed25519 -f "$STATE/new.key" >"$STATE/keygen.log" 2>&1
        "$BB" mv "$STATE/new.key" "$STATE/hostkey"
        echo ram > "$STATE/hostkey-source"
    fi
    if [ "$PERSIST" = 1 ] && [ ! -e "$KEYDIR/ed25519.key" ];then
        # Failure on a read-only/full card must not disable maintenance SSH.
        if ("$BB" mkdir -p "$KEYDIR" &&
            "$BB" cp "$STATE/hostkey" "$KEYDIR/ed25519.new" &&
            "$BB" mv "$KEYDIR/ed25519.new" "$KEYDIR/ed25519.key");then
            "$BB" sync
            echo sd > "$STATE/hostkey-source"
        else
            echo 'QC5_SSH: chiave temporanea in RAM; SD non scrivibile' >&2
        fi
    fi
    "$BB" chmod 600 "$STATE/hostkey"
}
case "${1:-check}" in
 check)
    server_alive
    ;;
 start)
    [ "$(cfg SSH_ENABLE)" = 1 ] || exit 0
    PORT=$(cfg SSH_PORT);PORT=${PORT:-22};positive "$PORT" 65535 || fail 'Porta non valida'
    # Local maintenance is independent of optional camera routing policies.
    [ -x "$BIN" ] && [ -f "$HACK/dropbear.md5" ] || fail 'Server non predisposto'
    A=$("$BB" md5sum "$BIN");E=$("$BB" cat "$HACK/dropbear.md5")
    [ "${A%% *}" = "$E" ] || fail 'Server non integro'
    [ -x "$CLEAN" ] && [ -f "$HACK/exec-clean.md5" ] || fail 'Avvio isolato non predisposto'
    A=$("$BB" md5sum "$CLEAN");E=$("$BB" cat "$HACK/exec-clean.md5")
    [ "${A%% *}" = "$E" ] || fail 'Avvio isolato non integro'
    LIMIT=$(ulimit -n);positive "$LIMIT" 4096 || fail 'Limite descrittori diverso dal profilo QC5'
    [ -f "$ETC/passwd" ] && [ ! -e "$ETC/shadow" ] || fail 'Profilo account diverso da QC5 stock'
    MAC=$("$BB" cat "$NET/wlan0/address")
    printf '%s\n' "$MAC" | "$BB" awk -F: 'NF!=6{exit 1} {for(i=1;i<=NF;i++)if($i!~/^[0-9a-fA-F][0-9a-fA-F]$/)exit 1}' || fail 'Identita Wi-Fi non valida'
    # FAT does not permit colons in file names; retain the full MAC as 12 hex digits.
    DEVICE=$(printf '%s' "$MAC" | "$BB" tr -d ':' | "$BB" tr A-F a-f)
    "$BB" mkdir -p "$STATE"
    server_alive && exit 0
    lock
    prepare_hostkey
    # Keep every vendor account. Add only the user-requested root alias;
    # no passwd or authentication data is embedded in the firmware image.
    "$BB" awk -F: '
        $1=="root" {if($3!=0)exit 2;seen=1;print "root::0:0:root:/:/bin/sh";next}
        {print}
        END {if(!seen)print "root::0:0:root:/:/bin/sh"}
    ' "$ETC/passwd" > "$STATE/passwd"
    "$BB" chmod 600 "$STATE/passwd"
    if [ ! -f "$STATE/passwd-bound" ];then
        if "$BB" awk -v p="$ETC/passwd" '$2==p{found=1} END{exit !found}' "$MOUNTS";then
            fail 'Overlay account gia presente'
        fi
        "$BB" mount -o bind "$STATE/passwd" "$ETC/passwd"
        "$BB" touch "$STATE/passwd-bound"
    fi
    # This build omits syslog and already logs to stderr; -E is unavailable.
    (trap '' HUP;exec "$CLEAN" "$BIN" dropbear -F -B -p "$PORT" -r "$STATE/hostkey" -P "$STATE/server.pid") </dev/null >"$STATE/server.log" 2>&1 &
    PID=$!;echo "$PID" > "$STATE/server.pid"
    sleep 1
    if ! kill -0 "$PID" 2>/dev/null;then
        restore_account
        "$BB" rm -f "$STATE/server.pid"
        fail 'Server non avviato; account originale ripristinato'
    fi
    echo 'QC5_SSH: root disponibile sulla LAN'
    ;;
 stop)
    lock
    if server_alive;then
        kill "$PID"
        n=0
        while kill -0 "$PID" 2>/dev/null && [ "$n" -lt 3 ];do sleep 1;n=$((n+1));done
        if server_alive;then kill -9 "$PID";fi
    fi
    restore_account
    "$BB" rm -f "$STATE/server.pid"
    echo 'QC5_SSH: servizio fermato e account originale ripristinato'
    ;;
 *) fail 'Uso: ssh.sh start|check|stop' ;;
esac
