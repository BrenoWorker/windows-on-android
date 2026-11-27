#!/bin/bash
set -uo pipefail

ROOT="${1:?runtime root filesystem}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
LOGS="${2:-/tmp/windroid-smoke}"
WORK="$(mktemp -d)"
SESSION=""
FAILED=0

if [ "$(uname -m)" != "aarch64" ]; then
    echo "The smoke test needs an arm64 machine" >&2
    exit 1
fi

mkdir -p "$LOGS" "$WORK/tmp" "$WORK/drive" "$WORK/prefix"

cleanup() {
    cp -r "$WORK/tmp/windroid/." "$LOGS/" 2>/dev/null || true
    [ -n "$SESSION" ] && kill "$SESSION" 2>/dev/null
    fuser -k -m "$ROOT" >/dev/null 2>&1 || true
    sleep 1
    for m in tmp mnt/c root/.wine proc sys dev/shm dev/pts dev; do umount -l "$ROOT/$m" 2>/dev/null || true; done
}
trap cleanup EXIT

pass() { echo "PASS  $1"; }
fail() {
    echo "FAIL  $1"
    FAILED=1
}

set -e
i686-w64-mingw32-gcc -O2 -s -o "$WORK/drive/console32.exe" "$REPO/runtime/tests/console.c"
x86_64-w64-mingw32-gcc -O2 -s -o "$WORK/drive/console64.exe" "$REPO/runtime/tests/console.c"
i686-w64-mingw32-gcc -O2 -s -o "$WORK/drive/https32.exe" "$REPO/runtime/tests/https.c" -lwinhttp
i686-w64-mingw32-gcc -O2 -s -municode -mwindows -DUNICODE -o "$WORK/drive/window32.exe" "$REPO/runtime/tests/window.c" -lgdi32
x86_64-w64-mingw32-gcc -O2 -s -municode -mwindows -DUNICODE -o "$WORK/drive/window64.exe" "$REPO/runtime/tests/window.c" -lgdi32
g++ -O2 -std=c++17 -o "$WORK/driver" "$REPO/runtime/tests/driver.cpp" "$REPO/app/src/main/cpp/core/XClient.cpp"
printf '@echo off\r\necho batch-ok\r\nexit /b 3\r\n' > "$WORK/drive/test.bat"

chmod 1777 "$WORK/tmp"
mkdir -p "$ROOT/root/.wine" "$ROOT/mnt/c" "$ROOT/dev/shm"
mount -t proc proc "$ROOT/proc"
mount -t sysfs sys "$ROOT/sys"
mount --bind /dev "$ROOT/dev"
mount --bind /dev/pts "$ROOT/dev/pts"
mount --bind "$WORK/tmp" "$ROOT/tmp"
mount --bind "$WORK/drive" "$ROOT/mnt/c"
mount --bind "$WORK/prefix" "$ROOT/root/.wine"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"
mkdir -p "$WORK/tmp/windroid/jobs" "$WORK/tmp/.X11-unix"
tr -d '\r' < "$REPO/app/src/main/assets/runtime/session.sh" > "$WORK/tmp/windroid/session.sh"
set +e

CONTROL="$WORK/tmp/windroid"
JOBS="$CONTROL/jobs"
mkfifo "$WORK/commands"
chroot "$ROOT" /usr/bin/env -i HOME=/root USER=root PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin TMPDIR=/tmp \
    DISPLAY=:1 LANG=C.UTF-8 WINEPREFIX=/root/.wine WINEDEBUG=-all 'WINEDLLOVERRIDES=mscoree=;mshtml=;winemenubuilder.exe=d' \
    BOX64_NOBANNER=1 BOX64_LOG=0 /bin/sh /tmp/windroid/session.sh 1024x768 /opt/wine/bin/wine /opt/wine/bin/wineserver \
    /usr/local/bin/box64 /usr/bin/Xvfb < "$WORK/commands" > "$LOGS/session.log" 2>&1 &
SESSION=$!
exec 9> "$WORK/commands"

wait_for() {
    local seconds="$1"
    shift
    local end=$((SECONDS + seconds))
    while [ "$SECONDS" -lt "$end" ]; do
        "$@" && return 0
        sleep 0.5
    done
    return 1
}

state_is() { [ "$(cat "$CONTROL/state" 2>/dev/null)" = "$1" ]; }
settled() { state_is ready || state_is failed || ! kill -0 "$SESSION" 2>/dev/null; }

echo "Starting the Windows session (first run sets up the prefix)"
wait_for 1200 settled
if state_is ready; then
    pass "session is ready after ${SECONDS}s"
else
    fail "session didn't become ready"
    cat "$LOGS/session.log"
    cat "$CONTROL/setup.log" 2>/dev/null | tail -n 60
    exit 1
fi

quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

launch() {
    local id="$1" kind="$2"
    shift 2
    {
        printf "cd '/mnt/c' 2>/dev/null\nexec '/usr/local/bin/box64' '/opt/wine/bin/wine'"
        local a
        for a in "$@"; do
            printf ' '
            quote "$a"
        done
        printf '\n'
    } > "$JOBS/$id.sh"
    echo "run $id $kind" >&9
}

has() { grep -q -- "$2" "$1" 2>/dev/null; }
exited() { [ -f "$JOBS/$1.exit" ]; }

console_test() {
    local id="$1" exe="$2" bits="$3"
    launch "$id" console "/mnt/c/$exe"
    if wait_for 180 has "$JOBS/$id.log" ready; then
        printf 'hello-%s\r\n' "$bits" > "$JOBS/$id.in"
    fi
    wait_for 120 exited "$id"
    if has "$JOBS/$id.log" "bits=$bits" && has "$JOBS/$id.log" "got:hello-$bits" && [ "$(cat "$JOBS/$id.exit" 2>/dev/null)" = 7 ]; then
        pass "$bits-bit console program: output, keyboard input and exit code"
    else
        fail "$bits-bit console program (exit $(cat "$JOBS/$id.exit" 2>/dev/null))"
        cat "$JOBS/$id.log" 2>/dev/null
    fi
}

console_test 1 console32.exe 32
console_test 2 console64.exe 64

launch 3 console cmd /c 'Z:\mnt\c\test.bat'
wait_for 120 exited 3
if has "$JOBS/3.log" batch-ok && [ "$(cat "$JOBS/3.exit" 2>/dev/null)" = 3 ]; then
    pass "batch file"
else
    fail "batch file (exit $(cat "$JOBS/3.exit" 2>/dev/null))"
    cat "$JOBS/3.log" 2>/dev/null
fi

launch 4 console /mnt/c/https32.exe
wait_for 180 exited 4
if has "$JOBS/4.log" 'https=[23]'; then
    pass "HTTPS from a 32-bit program"
else
    fail "HTTPS from a 32-bit program"
    cat "$JOBS/4.log" 2>/dev/null
fi

launch 5 gui explorer /desktop=WinDroid,1024x768 start /unix /mnt/c/window32.exe 'Z:\mnt\c\first.txt'
if wait_for 180 has "$WORK/drive/first.txt" shown; then
    pass "32-bit graphical program opened"
    if "$WORK/driver" "$WORK/tmp/.X11-unix/X1" "$WORK/tmp/Xvfb_screen0"; then
        pass "program is visible on the framebuffer"
    else
        fail "program is not visible on the framebuffer"
    fi
    if has "$WORK/drive/first.txt" click; then pass "mouse click reached the program"; else fail "mouse click didn't reach the program"; fi
    missing=""
    for code in U+006F U+00E7 U+00E3 U+00C9; do
        has "$WORK/drive/first.txt" "$code" || missing="$missing $code"
    done
    if [ -z "$missing" ]; then pass "keyboard, including accented letters"; else fail "keyboard is missing$missing"; fi
    cat "$WORK/drive/first.txt"
else
    fail "32-bit graphical program didn't open"
    cat "$JOBS/5.log" 2>/dev/null
fi

program_running() {
    local dir
    for dir in /proc/[0-9]*; do
        tr '\0' '\n' < "$dir/cmdline" 2>/dev/null | head -n 4 | grep -qi "$1\$" && return 0
    done
    return 1
}

launch 6 gui explorer /desktop=WinDroid,1024x768 start /unix /mnt/c/window64.exe 'Z:\mnt\c\second.txt'
if wait_for 180 has "$WORK/drive/second.txt" shown; then
    sleep 15
    if program_running window32.exe && program_running window64.exe; then
        pass "a second program runs alongside the first and both stay open"
    else
        fail "the two programs didn't stay open together"
    fi
else
    fail "second graphical program didn't open"
    cat "$JOBS/6.log" 2>/dev/null
fi

echo "Process command lines used to count running programs:"
for dir in /proc/[0-9]*; do
    line="$(tr '\0' ' ' < "$dir/cmdline" 2>/dev/null)"
    case "$line" in *.exe*) echo "  $line" | cut -c1-200 ;; esac
done

launch 7 console /mnt/c/console32.exe
wait_for 180 has "$JOBS/7.log" ready
echo "kill 7" >&9
if wait_for 30 exited 7; then pass "stopping a console program"; else fail "console program couldn't be stopped"; fi

echo quit >&9
exec 9>&-
ended() { ! kill -0 "$SESSION" 2>/dev/null; }
if wait_for 60 ended && state_is off; then pass "session shut down cleanly"; else fail "session didn't shut down"; fi

if [ "$FAILED" -ne 0 ]; then
    cat "$LOGS/session.log"
    exit 1
fi
echo "All runtime tests passed"
