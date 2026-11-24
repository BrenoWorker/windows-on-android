#!/bin/bash
set -euo pipefail

OUT="${1:?output directory}"
SUITE="${SUITE:-trixie}"
ROOT="${ROOTFS:-/tmp/windroid-rootfs}"
BOX64_TAG="${BOX64_TAG:-v0.4.4}"
NAME="windroid-runtime-arm64.tar.gz"
WORK="$(mktemp -d)"

cleanup() {
    for m in proc sys dev/pts dev; do umount "$ROOT/$m" 2>/dev/null || true; done
}
trap cleanup EXIT

rm -rf "$ROOT"
mkdir -p "$OUT"

git clone --depth 1 --branch "$BOX64_TAG" https://github.com/ptitSeb/box64 "$WORK/box64"
cmake -S "$WORK/box64" -B "$WORK/box64/build" \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DARM64=1 -DARM_DYNAREC=1 -DBAD_SIGNAL=1 \
    -DCMAKE_BUILD_TYPE=Release -DCI=True
cmake --build "$WORK/box64/build" -j"$(nproc)" --target box64

debootstrap --foreign --arch=arm64 --variant=minbase --include=ca-certificates "$SUITE" "$ROOT" http://deb.debian.org/debian
if [ -x /usr/bin/qemu-aarch64-static ]; then cp /usr/bin/qemu-aarch64-static "$ROOT/usr/bin/"; fi
chroot "$ROOT" /debootstrap/debootstrap --second-stage
mount -t proc proc "$ROOT/proc"
mount -t sysfs sys "$ROOT/sys"
mount --bind /dev "$ROOT/dev"
mount --bind /dev/pts "$ROOT/dev/pts"

chroot "$ROOT" /bin/bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture amd64
apt-get update
apt-get install -y --no-install-recommends xvfb xkb-data x11-xkb-utils xauth fonts-liberation2 fontconfig
if apt-cache show wine64:amd64 >/dev/null 2>&1; then
    apt-get install -y --no-install-recommends wine64:amd64 libwine:amd64
else
    apt-get install -y --no-install-recommends wine:amd64
fi
apt-get install -y --no-install-recommends libfreetype6:amd64 libfontconfig1:amd64 libgnutls30t64:amd64 || \
    apt-get install -y --no-install-recommends libfreetype6:amd64 libfontconfig1:amd64
apt-get clean
'
VERSION="$(chroot "$ROOT" dpkg-query -W -f='${Version}' libwine:amd64 2>/dev/null || chroot "$ROOT" dpkg-query -W -f='${Version}' wine:amd64)"
cleanup

install -m 0755 "$WORK/box64/build/box64" "$ROOT/usr/local/bin/box64"
mkdir -p "$ROOT/usr/lib/box64-x86_64-linux-gnu"
cp -a "$WORK/box64/x64lib/." "$ROOT/usr/lib/box64-x86_64-linux-gnu/" 2>/dev/null || true
if [ -f "$WORK/box64/system/box64.box64rc" ]; then install -m 0644 "$WORK/box64/system/box64.box64rc" "$ROOT/etc/box64.box64rc"; fi

rm -f "$ROOT/usr/bin/qemu-aarch64-static"
rm -rf "$ROOT"/var/lib/apt/lists/* "$ROOT"/var/cache/apt/* "$ROOT"/var/log/*
rm -rf "$ROOT"/usr/share/doc/* "$ROOT"/usr/share/man/* "$ROOT"/usr/share/info/*
find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 ! -name 'en*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$ROOT"/dev/* "$ROOT"/proc/* "$ROOT"/sys/* "$ROOT"/run/* "$ROOT"/tmp/*
mkdir -p "$ROOT/tmp" "$ROOT/mnt/c" "$ROOT/root" "$ROOT/dev/shm"
chmod 1777 "$ROOT/tmp"

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
    file -b "$real" | grep -q 'ELF 64-bit.*x86-64' || return 1
    echo "/${real#"$ROOT"/}"
}

WINE=""
for c in usr/lib/wine/wine64 usr/lib/wine/wine usr/lib/x86_64-linux-gnu/wine/wine64 usr/lib/x86_64-linux-gnu/wine/wine usr/bin/wine64 usr/bin/wine; do
    if WINE="$(elf "$c")"; then break; fi
    WINE=""
done
SERVER=""
for c in usr/lib/wine/wineserver64 usr/lib/wine/wineserver usr/lib/x86_64-linux-gnu/wine/wineserver usr/bin/wineserver; do
    if SERVER="$(elf "$c")"; then break; fi
    SERVER=""
done
if [ -z "$WINE" ]; then
    find "$ROOT/usr" -name 'wine*' -type f | head -50
    echo "Wine binary not found" >&2
    exit 1
fi

{
    echo "format=2"
    echo "version=Wine $VERSION, Box64 $BOX64_TAG ($SUITE)"
    echo "wine=$WINE"
    if [ -n "$SERVER" ]; then echo "wineserver=$SERVER"; fi
    echo "box64=/usr/local/bin/box64"
    echo "xvfb=/usr/bin/Xvfb"
} > "$ROOT/windroid-runtime.ini"
cat "$ROOT/windroid-runtime.ini"

tar --numeric-owner -C "$ROOT" -czf "$OUT/$NAME" .
ls -lh "$OUT/$NAME"
