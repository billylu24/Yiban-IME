#!/usr/bin/env bash
# Requires the local Fcitx prefix and an already-built bilingualcontext addon.
set -euo pipefail
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" != --session ]]; then
    build_dir="${1:-$project/.local-env/build/bilingual}"
    exec dbus-run-session -- bash "$0" --session "$build_dir"
fi
build_dir="$(cd "$2" && pwd)"
test -f "$build_dir/libbilingualcontext.so"
scratch="$(mktemp -d /tmp/fcitx-cursor-e2e.XXXXXX)"
export XDG_CONFIG_HOME="$scratch/config" XDG_DATA_HOME="$scratch/data"
export XDG_CACHE_HOME="$scratch/cache" XDG_RUNTIME_DIR="$scratch/runtime"
export XDG_DATA_DIRS="$project/.local-env/prefix/share:/usr/local/share:/usr/share"
export FCITX_ADDON_DIRS="$build_dir:$project/.local-env/prefix/lib/fcitx5"
export LD_LIBRARY_PATH="$project/.local-env/prefix/lib:$project/.local-env/sysroot/usr/lib/x86_64-linux-gnu"
# Never use a caller's input-method configuration, display, or translator.
unset DISPLAY WAYLAND_DISPLAY FCITX_CONFIG_HOME FCITX_CONFIG_DIRS FCITX_DATA_HOME FCITX_DATA_DIRS
export BILINGUAL_TRANSLATOR_SOCKET="$XDG_RUNTIME_DIR/bilingual-ime/translator.sock"
mkdir -p "$XDG_CONFIG_HOME/fcitx5" "$XDG_RUNTIME_DIR" "$XDG_DATA_HOME/fcitx5/rime"
chmod 700 "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf"
cp "$project/config/bilingualcontext.conf" "$XDG_CONFIG_HOME/fcitx5/conf/bilingualcontext.conf"
if [[ "${YIBAN_TEST_DISABLED:-0}" == 1 ]]; then
    sed -i "s/^Enabled=True/Enabled=False/" "$XDG_CONFIG_HOME/fcitx5/conf/bilingualcontext.conf"
fi
cp "$project/config/smoke/profile" "$XDG_CONFIG_HOME/fcitx5/profile"
cp "$project/config/smoke/default.custom.yaml" "$XDG_DATA_HOME/fcitx5/rime/default.custom.yaml"
python3 "$project/translator/daemon.py" --fake --socket "$BILINGUAL_TRANSLATOR_SOCKET" >"$scratch/daemon.log" 2>&1 &
daemon_pid=$!
fcitx_pid=''
trap 'kill "$daemon_pid" ${fcitx_pid:+"$fcitx_pid"} 2>/dev/null || true; wait "$daemon_pid" ${fcitx_pid:+"$fcitx_pid"} 2>/dev/null || true' EXIT
"$project/.local-env/prefix/bin/fcitx5" -D -k --disable all \
    --enable keyboard,rime,bilingualcontext,dbus,dbusfrontend \
    --verbose default=5 >"$scratch/fcitx.log" 2>&1 &
fcitx_pid=$!
sleep 2
echo "Logs: $scratch"
timeout 25s gjs "$project/tests/e2e_cursor_translation.js"
