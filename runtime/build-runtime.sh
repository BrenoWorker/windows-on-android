#!/bin/bash
set -euo pipefail

OUT="${1:?output directory}"
SUITE="${SUITE:-trixie}"
ROOT="${ROOTFS:-/tmp/windroid-rootfs}"
BOX64_TAG="${BOX64_TAG:-v0.4.4}"
WINE_VERSION="${WINE_VERSION:-11.0}"
WINE_SHA256="${WINE_SHA256:-39574efa1132c3ca0d5c77dd2eddbe4a49cca0d6cc2c290ff4924493a1c40314}"
WINE_URL="https://github.com/Kron4ek/Wine-Builds/releases/download/$WINE_VERSION/wine-$WINE_VERSION-amd64-wow64.tar.xz"
NAME="windroid-runtime-arm64.tar.gz"
WORK="$(mktemp -d)"

cleanup() {
    for m in proc sys dev/pts dev; do umount "$ROOT/$m" 2>/dev/null || true; done
}
trap cleanup EXIT

rm -rf "$ROOT"
mkdir -p "$OUT"

CROSS=()
if [ "$(uname -m)" != "aarch64" ]; then
    CROSS=(-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64)
fi
git clone --depth 1 --branch "$BOX64_TAG" https://github.com/ptitSeb/box64 "$WORK/box64"
cmake -S "$WORK/box64" -B "$WORK/box64/build" "${CROSS[@]}" \
    -DARM64=1 -DARM_DYNAREC=1 -DBAD_SIGNAL=1 \
    -DCMAKE_BUILD_TYPE=Release -DCI=True
cmake --build "$WORK/box64/build" -j"$(nproc)" --target box64

curl -fsSL --retry 3 -o "$WORK/wine.tar.xz" "$WINE_URL"
echo "$WINE_SHA256  $WORK/wine.tar.xz" | sha256sum -c -

debootstrap --foreign --arch=arm64 --variant=minbase --include=ca-certificates "$SUITE" "$ROOT" http://deb.debian.org/debian
if [ "$(uname -m)" != "aarch64" ] && [ -x /usr/bin/qemu-aarch64-static ]; then cp /usr/bin/qemu-aarch64-static "$ROOT/usr/bin/"; fi
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
apt-get install -y --no-install-recommends \
    libx11-6 libxext6 libxi6 libxcursor1 libxfixes3 libxrandr2 libxrender1 libxinerama1 libxcomposite1 libxxf86vm1 \
    libfreetype6 libfontconfig1 libgnutls30t64 libgl1 libegl1 libgl1-mesa-dri libvulkan1 \
    libasound2t64 libpulse0 libdbus-1-3 libudev1 libkrb5-3 libgssapi-krb5-2 libcups2t64
apt-get install -y --no-install-recommends libc6:amd64 libgcc-s1:amd64 libstdc++6:amd64
apt-get clean
'
cleanup

mkdir -p "$ROOT/opt/wine"
tar -xJf "$WORK/wine.tar.xz" -C "$ROOT/opt/wine" --strip-components=1 --no-same-owner
rm -rf "$ROOT/opt/wine/include" "$ROOT/opt/wine/share/man" "$ROOT/opt/wine/share/applications"
for tool in function_grep.pl widl winebuild winecpp winedump wineg++ winegcc winemaker wmc wrc; do
    rm -f "$ROOT/opt/wine/bin/$tool"
done
find "$ROOT/opt/wine/lib" -name '*.a' -delete

install -D -m 0755 "$WORK/box64/build/box64" "$ROOT/usr/local/bin/box64"
mkdir -p "$ROOT/usr/lib/box64-x86_64-linux-gnu"
cp -a "$WORK/box64/x64lib/." "$ROOT/usr/lib/box64-x86_64-linux-gnu/" 2>/dev/null || true
if [ -f "$WORK/box64/system/box64.box64rc" ]; then install -m 0644 "$WORK/box64/system/box64.box64rc" "$ROOT/etc/box64.box64rc"; fi

rm -f "$ROOT/usr/bin/qemu-aarch64-static"
rm -rf "$ROOT"/var/lib/apt/lists/* "$ROOT"/var/cache/apt/* "$ROOT"/var/log/*
rm -rf "$ROOT"/usr/share/doc/* "$ROOT"/usr/share/man/* "$ROOT"/usr/share/info/*
find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 ! -name 'en*' -exec rm -rf {} + 2>/dev/null || true
rm -rf "$ROOT"/dev/* "$ROOT"/proc/* "$ROOT"/sys/* "$ROOT"/run/* "$ROOT"/tmp/*
mkdir -p "$ROOT/tmp" "$ROOT/mnt/c" "$ROOT/root/.wine" "$ROOT/dev/shm"
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

for binary in opt/wine/bin/wine opt/wine/bin/wineserver; do
    if ! file -b "$ROOT/$binary" | grep -q 'ELF 64-bit.*x86-64'; then
        echo "$binary is not an x86-64 program" >&2
        exit 1
    fi
done
for dir in i386-windows x86_64-windows; do
    if [ ! -f "$ROOT/opt/wine/lib/wine/$dir/ntdll.dll" ]; then
        echo "Wine is missing $dir" >&2
        exit 1
    fi
done

{
    echo "format=3"
    echo "version=Wine $WINE_VERSION (WoW64), Box64 $BOX64_TAG ($SUITE)"
    echo "wow64=1"
    echo "wine=/opt/wine/bin/wine"
    echo "wineserver=/opt/wine/bin/wineserver"
    echo "box64=/usr/local/bin/box64"
    echo "xvfb=/usr/bin/Xvfb"
} > "$ROOT/windroid-runtime.ini"
cat "$ROOT/windroid-runtime.ini"

tar --numeric-owner -C "$ROOT" -czf "$OUT/$NAME" .
ls -lh "$OUT/$NAME"
