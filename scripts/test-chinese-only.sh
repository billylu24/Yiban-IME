#!/usr/bin/env bash
# Requires the local Fcitx prefix and a built Chinese-only Rime addon.
set -euo pipefail
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" != --session ]]; then
    addon_dir="${1:-$project/.local-env/prefix/lib/fcitx5}"
    exec dbus-run-session -- bash "$0" --session "$addon_dir"
fi
addon_dir="$(cd "$2" && pwd)"
test -f "$addon_dir/librime.so"
scratch="$(mktemp -d /tmp/yiban-chinese-only.XXXXXX)"
export XDG_CONFIG_HOME="$scratch/config" XDG_DATA_HOME="$scratch/data"
export XDG_CACHE_HOME="$scratch/cache" XDG_RUNTIME_DIR="$scratch/runtime"
export XDG_DATA_DIRS="$project/.local-env/prefix/share:/usr/local/share:/usr/share"
export FCITX_ADDON_DIRS="$addon_dir:$project/.local-env/prefix/lib/fcitx5"
export LD_LIBRARY_PATH="$project/.local-env/prefix/lib:$project/.local-env/sysroot/usr/lib/x86_64-linux-gnu"
unset DISPLAY WAYLAND_DISPLAY FCITX_CONFIG_HOME FCITX_CONFIG_DIRS FCITX_DATA_HOME FCITX_DATA_DIRS
mkdir -p "$XDG_CONFIG_HOME/fcitx5" "$XDG_DATA_HOME/fcitx5/rime" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$project/config/smoke/profile" "$XDG_CONFIG_HOME/fcitx5/profile"
cp "$project/config/smoke/default.custom.yaml" "$XDG_DATA_HOME/fcitx5/rime/default.custom.yaml"
# Disable Fcitx's default Shift trigger, as in the daily Ctrl+Space setup.
cat >"$XDG_CONFIG_HOME/fcitx5/config" <<'EOF'
[Hotkey]
AltTriggerKeys=

[Hotkey/TriggerKeys]
0=Control+space
EOF
# The small bundled dictionary suffices to test mode behavior.
sed 's/translator\/dictionary: rime_ice/translator\/dictionary: pinyin_simp/' \
    "$project/config/rime/pinyin_simp.custom.yaml" \
    >"$XDG_DATA_HOME/fcitx5/rime/pinyin_simp.custom.yaml"
"$project/.local-env/prefix/bin/rime_deployer" --build \
    "$XDG_DATA_HOME/fcitx5/rime" "$project/.local-env/prefix/share/rime-data" \
    "$XDG_DATA_HOME/fcitx5/rime/build" >"$scratch/deploy.log" 2>&1
"$project/.local-env/prefix/bin/fcitx5" -D -k --disable all \
    --enable keyboard,rime,dbus,dbusfrontend >"$scratch/fcitx.log" 2>&1 &
fcitx_pid=$!
trap 'kill "$fcitx_pid" 2>/dev/null || true; wait "$fcitx_pid" 2>/dev/null || true' EXIT
sleep 2
echo "Logs: $scratch"
timeout 25s gjs "$project/tests/e2e_chinese_only.js"
