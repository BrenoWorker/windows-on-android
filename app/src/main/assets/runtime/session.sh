#!/bin/sh
SCREEN="$1"
WINE="$2"
WINESERVER="$3"
BOX64="$4"
XVFB="$5"
BASE=/tmp/windroid
JOBS="$BASE/jobs"

state() {
    echo "$1" > "$BASE/state.tmp" && mv "$BASE/state.tmp" "$BASE/state"
}

fail() {
    echo "$1"
    state failed
    exit 1
}

mkdir -p "$JOBS" /tmp/.X11-unix
state starting

"$XVFB" :1 -screen 0 "${SCREEN}x24" -fbdir /tmp -nolisten tcp -ac -dpi 96 > "$BASE/xvfb.log" 2>&1 &
XPID=$!

tries=0
while [ ! -S /tmp/.X11-unix/X1 ] || [ ! -s /tmp/Xvfb_screen0 ]; do
    if ! kill -0 "$XPID" 2>/dev/null; then
        tail -n 5 "$BASE/xvfb.log"
        fail "The display server stopped"
    fi
    tries=$((tries + 1))
    [ "$tries" -gt 600 ] && fail "The display server didn't start"
    sleep 0.1
done

VERSION="$(grep '^version=' /windroid-runtime.ini 2>/dev/null)"
STAMP="$WINEPREFIX/.windroid"
if [ ! -f "$WINEPREFIX/system.reg" ] || [ "$(cat "$STAMP" 2>/dev/null)" != "$VERSION" ]; then
    state setup
    if [ -f "$WINEPREFIX/system.reg" ]; then mode=-u; else mode=-i; fi
    mkdir -p "$WINEPREFIX"
    timeout 900 "$BOX64" "$WINE" wineboot "$mode" < /dev/null > "$BASE/setup.log" 2>&1
    code=$?
    timeout 300 "$BOX64" "$WINESERVER" -w < /dev/null >> "$BASE/setup.log" 2>&1
    if [ "$code" -ne 0 ] || [ ! -f "$WINEPREFIX/system.reg" ]; then
        tail -n 8 "$BASE/setup.log"
        fail "Windows couldn't be set up (code $code)"
    fi
    echo "$VERSION" > "$STAMP"
fi

if [ -d "$WINEPREFIX/dosdevices" ] && [ ! -e "$WINEPREFIX/dosdevices/d:" ]; then
    ln -s /mnt/c "$WINEPREFIX/dosdevices/d:"
fi

"$BOX64" "$WINESERVER" -p < /dev/null > /dev/null 2>&1
state ready

start_job() {
    if [ "$2" = console ]; then
        mkfifo "$JOBS/$1.in"
    fi
    (
        if [ -p "$JOBS/$1.in" ]; then
            exec 3<> "$JOBS/$1.in"
        else
            exec 3< /dev/null
        fi
        setsid /bin/sh "$JOBS/$1.sh" <&3 >> "$JOBS/$1.log" 2>&1 &
        child=$!
        echo "$child" > "$JOBS/$1.pid"
        wait "$child"
        echo "$?" > "$JOBS/$1.exit.tmp"
        mv "$JOBS/$1.exit.tmp" "$JOBS/$1.exit"
    ) < /dev/null > /dev/null 2>&1 &
}

stop_job() {
    pid="$(cat "$JOBS/$1.pid" 2>/dev/null)"
    [ -n "$pid" ] || return 0
    kill -s KILL -- "-$pid" 2>/dev/null || kill -s KILL "$pid" 2>/dev/null
}

while read -r command id kind; do
    case "$command" in
        run) start_job "$id" "$kind" ;;
        kill) stop_job "$id" ;;
        quit) break ;;
    esac
done

timeout 20 "$BOX64" "$WINESERVER" -k < /dev/null > /dev/null 2>&1
kill "$XPID" 2>/dev/null
state off
