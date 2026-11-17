#!/bin/sh
# Run as the unprivileged desktop user after the boot environment supplies DRM,
# input permissions and a seatd instance. This script does not load drivers.
set -eu
if [ "$(id -u)" = 0 ]; then printf 'Run the desktop as the kbl user.\n' >&2; exit 1; fi
: "${XDG_RUNTIME_DIR:?Set a private runtime directory owned by the desktop user}"
export LIBSEAT_BACKEND=seatd
export LIBGL_ALWAYS_SOFTWARE=1
export MOZ_ENABLE_WAYLAND=1
export GDK_BACKEND=wayland
export WAYLAND_DISPLAY=kbl-wayland
backend=${KBL_WESTON_BACKEND:-drm-backend.so}
weston --backend="$backend" --use-pixman --socket="$WAYLAND_DISPLAY" --idle-time=0 &
compositor=$!
cleanup() { kill "$compositor" 2>/dev/null || true; }
trap cleanup EXIT HUP INT TERM
attempt=0
while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
    kill -0 "$compositor" 2>/dev/null || { printf 'Compositor exited before its Wayland socket opened.\n' >&2; exit 1; }
    attempt=$((attempt + 1));[ "$attempt" -lt 100 ] || { printf 'Wayland socket timed out.\n' >&2; exit 1; }
    sleep .1
done
mkdir -p "$HOME/.mozilla/kbl-live"
firefox-esr --no-remote --profile "$HOME/.mozilla/kbl-live" file:///usr/share/kbl/welcome.html
