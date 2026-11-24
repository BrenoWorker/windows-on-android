#!/bin/bash
set -euo pipefail

OUT="${1:?output directory}"
SUITE="${SUITE:-trixie}"
ROOT="${ROOTFS:-/tmp/windroid-rootfs}"
NAME="windroid-runtime-x86_64.tar.gz"

cleanup() {
    umount "$ROOT/proc" 2>/dev/null || true
    umount "$ROOT/sys" 2>/dev/null || true
}
trap cleanup EXIT

rm -rf "$ROOT"
mkdir -p "$OUT"
debootstrap --variant=minbase --arch=amd64 "$SUITE" "$ROOT" http://deb.debian.org/debian
mount -t proc proc "$ROOT/proc"
mount -t sysfs sys "$ROOT/sys"

chroot "$ROOT" /bin/bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update
if apt-cache show wine64 >/dev/null 2>&1; then
    apt-get install -y --no-install-recommends wine64 libwine fonts-liberation2 ca-certificates
else
    apt-get install -y --no-install-recommends wine fonts-liberation2 ca-certificates
fi
apt-get clean
'
VERSION="$(chroot "$ROOT" dpkg-query -W -f='${Version}' libwine 2>/dev/null || chroot "$ROOT" dpkg-query -W -f='${Version}' wine)"
cleanup

rm -rf "$ROOT"/var/lib/apt/lists/* "$ROOT"/var/cache/apt/* "$ROOT"/var/log/*
rm -rf "$ROOT"/usr/share/doc/* "$ROOT"/usr/share/man/* "$ROOT"/usr/share/info/*
find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 ! -name 'en*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$ROOT"/dev/* "$ROOT"/proc/* "$ROOT"/sys/* "$ROOT"/run/* "$ROOT"/tmp/*

python3 - "$ROOT" <<'PY'
import os, sys
root = sys.argv[1]
for base, dirs, files in os.walk(root):
    for n in dirs + files:
        p = os.path.join(base, n)
        if not os.path.islink(p):
            continue
        t = os.readlink(p)
        if not t.startswith("/"):
            continue
        rel = os.path.relpath(root + t, os.path.dirname(p))
        os.unlink(p)
        os.symlink(rel, p)
PY

elf() {
    local p="$ROOT/$1"
    [ -e "$p" ] || return 1
    local real
    real="$(readlink -f "$p")"
    case "$real" in "$ROOT"/*) ;; *) return 1 ;; esac
    file -b "$real" | grep -q '^ELF 64-bit' || return 1
    echo "${real#"$ROOT"/}"
}

WINE=""
for c in usr/lib/wine/wine64 usr/lib/wine/wine usr/lib/x86_64-linux-gnu/wine/wine64 usr/lib/x86_64-linux-gnu/wine/wine usr/bin/wine64 usr/bin/wine; do
    if WINE="$(elf "$c")"; then break; fi
    WINE=""
done
SERVER=""
for c in usr/lib/wine/wineserver64 usr/lib/wine/wineserver usr/lib/x86_64-linux-gnu/wine/wineserver usr/bin/wineserver64 usr/bin/wineserver; do
    if SERVER="$(elf "$c")"; then break; fi
    SERVER=""
done
if [ -z "$WINE" ]; then
    find "$ROOT/usr" -name 'wine*' -type f | head -50
    echo "Wine binary not found" >&2
    exit 1
fi

LIBS=""
for d in lib/x86_64-linux-gnu usr/lib/x86_64-linux-gnu usr/lib/x86_64-linux-gnu/wine/x86_64-unix usr/lib/wine/x86_64-unix usr/lib/x86_64-linux-gnu/wine usr/lib/wine; do
    [ -d "$ROOT/$d" ] && LIBS="${LIBS:+$LIBS:}@ROOT@/$d"
done
DLLS=""
for d in usr/lib/x86_64-linux-gnu/wine usr/lib/wine; do
    [ -d "$ROOT/$d" ] && DLLS="${DLLS:+$DLLS:}@ROOT@/$d"
done

{
    echo "version=Wine $VERSION ($SUITE)"
    echo "wine=$WINE"
    echo "env.BOX64_LD_LIBRARY_PATH=$LIBS"
    echo "env.BOX64_PATH=@ROOT@/usr/bin:@ROOT@/$(dirname "$WINE")"
    [ -n "$SERVER" ] && echo "env.WINESERVER=@ROOT@/$SERVER"
    [ -n "$DLLS" ] && echo "env.WINEDLLPATH=$DLLS"
    echo "env.FONTCONFIG_PATH=@ROOT@/etc/fonts"
    echo "env.SSL_CERT_FILE=@ROOT@/etc/ssl/certs/ca-certificates.crt"
} > "$ROOT/windroid-runtime.ini"
cat "$ROOT/windroid-runtime.ini"

tar --numeric-owner -C "$ROOT" -czf "$OUT/$NAME" .
ls -lh "$OUT/$NAME"
