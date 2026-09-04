#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PREFIX="$PROJECT_ROOT/.local-env/prefix"
ADDON_DIR="$PREFIX/lib/fcitx5"
SYSTEM_ADDON_DIR="/usr/lib/x86_64-linux-gnu/fcitx5"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5"
AUTOSTART_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
STATE_ROOT="${XDG_STATE_HOME:-$HOME/.local/state}/giaok-keyboard"
ACTIVE_STATE="$STATE_ROOT/active-main-user-test"
LAUNCHER_DIR="$HOME/.local/bin"
LAUNCHER="$LAUNCHER_DIR/fcitx5-giaok-5.1.22"
TRANSLATOR_LAUNCHER="$LAUNCHER_DIR/bilingual-ime-translator"
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
TRANSLATOR_SERVICE="$SYSTEMD_USER_DIR/bilingual-ime-translator.service"
PROFILE="$CONFIG_DIR/profile"
AUTOSTART="$AUTOSTART_DIR/org.fcitx.Fcitx5.desktop"
RIME_USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/rime"
RIME_CUSTOM="$RIME_USER_DIR/default.custom.yaml"

if [[ -e "$ACTIVE_STATE" ]]; then
    echo "A Giaok main-environment test is already active: $ACTIVE_STATE" >&2
    echo "Run scripts/restore-main-user-test.sh before installing again." >&2
    exit 1
fi

test -x "$PREFIX/bin/fcitx5"
test -f "$ADDON_DIR/librime.so"
test -d "$SYSTEM_ADDON_DIR"

version="$("$PREFIX/bin/fcitx5" --version)"
if [[ "$version" != "5.1.22" ]]; then
    echo "Expected Fcitx5 5.1.22, found $version" >&2
    exit 1
fi

# The local core was built with a private addon directory. Reuse only system
# addons that are not already provided by the local 5.1.22 build.
for module in "$SYSTEM_ADDON_DIR"/*.so; do
    name="${module##*/}"
    if [[ ! -e "$ADDON_DIR/$name" ]]; then
        ln -s "$module" "$ADDON_DIR/$name"
    fi
done

# Fail before touching user configuration if any reused addon has an unresolved
# dynamic symbol against the 5.1.22 core.
while IFS= read -r link; do
    if LD_LIBRARY_PATH="$PREFIX/lib:$PROJECT_ROOT/.local-env/sysroot/usr/lib/x86_64-linux-gnu" \
        ldd -r "$link" 2>&1 | grep -Eq 'undefined symbol|not found'; then
        echo "Incompatible system addon: $link" >&2
        LD_LIBRARY_PATH="$PREFIX/lib:$PROJECT_ROOT/.local-env/sysroot/usr/lib/x86_64-linux-gnu" \
            ldd -r "$link" >&2 || true
        exit 1
    fi
done < <(find "$ADDON_DIR" -maxdepth 1 -type l -name '*.so' -print)

timestamp="$(date +%Y%m%d-%H%M%S)"
backup_dir="$STATE_ROOT/backups/$timestamp"
mkdir -p "$backup_dir" "$CONFIG_DIR" "$AUTOSTART_DIR" "$LAUNCHER_DIR" \
    "$SYSTEMD_USER_DIR"

if [[ -d "$RIME_USER_DIR" ]]; then
    cp -a "$RIME_USER_DIR" "$backup_dir/rime-user-dir"
    rime_dir_existed=1
else
    rime_dir_existed=0
fi
mkdir -p "$RIME_USER_DIR"

if [[ -e "$PROFILE" ]]; then
    cp -a "$PROFILE" "$backup_dir/profile"
    profile_existed=1
else
    profile_existed=0
fi

if [[ -e "$AUTOSTART" ]]; then
    cp -a "$AUTOSTART" "$backup_dir/autostart.desktop"
    autostart_existed=1
else
    autostart_existed=0
fi

if [[ -e "$LAUNCHER" ]]; then
    cp -a "$LAUNCHER" "$backup_dir/launcher"
    launcher_existed=1
else
    launcher_existed=0
fi

if [[ -e "$TRANSLATOR_LAUNCHER" ]]; then
    cp -a "$TRANSLATOR_LAUNCHER" "$backup_dir/translator-launcher"
    translator_launcher_existed=1
else
    translator_launcher_existed=0
fi

if [[ -e "$TRANSLATOR_SERVICE" ]]; then
    cp -a "$TRANSLATOR_SERVICE" "$backup_dir/translator.service"
    translator_service_existed=1
else
    translator_service_existed=0
fi

if [[ -e "$RIME_CUSTOM" ]]; then
    cp -a "$RIME_CUSTOM" "$backup_dir/default.custom.yaml"
    rime_custom_existed=1
else
    rime_custom_existed=0
fi

install -m 0755 "$SCRIPT_DIR/run-main-fcitx5-5.1.22.sh" "$LAUNCHER"
install -m 0755 "$SCRIPT_DIR/run-translator-daemon.sh" "$TRANSLATOR_LAUNCHER"
install -m 0644 "$PROJECT_ROOT/data/bilingual-ime-translator.service" \
    "$TRANSLATOR_SERVICE"
systemctl --user daemon-reload
systemctl --user enable --now bilingual-ime-translator.service

cat >"$AUTOSTART" <<EOF
[Desktop Entry]
Name=Giaok Fcitx 5.1.22 Test
Comment=Temporary reversible bilingual Rime test
Exec=$LAUNCHER -d
Icon=fcitx
Terminal=false
Type=Application
StartupNotify=false
X-GNOME-Autostart-enabled=true
X-GNOME-AutoRestart=true
EOF

cat >"$ACTIVE_STATE" <<EOF
backup_dir=$backup_dir
profile_existed=$profile_existed
autostart_existed=$autostart_existed
launcher_existed=$launcher_existed
translator_launcher_existed=$translator_launcher_existed
translator_service_existed=$translator_service_existed
rime_custom_existed=$rime_custom_existed
rime_dir_existed=$rime_dir_existed
project_root=$PROJECT_ROOT
EOF

if command -v fcitx5-remote >/dev/null 2>&1; then
    fcitx5-remote -e >/dev/null 2>&1 || true
fi
for _ in {1..30}; do
    pgrep -u "$(id -u)" -x fcitx5 >/dev/null || break
    sleep 0.1
done
if pgrep -u "$(id -u)" -x fcitx5 >/dev/null; then
    pkill -TERM -u "$(id -u)" -x fcitx5
    sleep 1
fi

# Write the new profile only after the old process has exited, otherwise its
# shutdown autosave can restore the previous input-method list.
install -m 0644 "$PROJECT_ROOT/config/user-test-profile" "$PROFILE"
install -m 0644 "$PROJECT_ROOT/config/smoke/default.custom.yaml" "$RIME_CUSTOM"
"$LAUNCHER" -d

sleep 2
if ! pgrep -u "$(id -u)" -x fcitx5 >/dev/null; then
    echo "Fcitx5 5.1.22 did not stay running; restoring the previous setup." >&2
    "$SCRIPT_DIR/restore-main-user-test.sh"
    exit 1
fi
running_pid="$(pgrep -u "$(id -u)" -x fcitx5 | tail -1)"
running_exe="$(readlink -f "/proc/$running_pid/exe")"
if [[ "$running_exe" != "$PREFIX/bin/fcitx5" ]]; then
    echo "Unexpected Fcitx runtime: $running_exe" >&2
    "$SCRIPT_DIR/restore-main-user-test.sh"
    exit 1
fi

echo "Active: Fcitx5 5.1.22 with bilingual Rime"
echo "Backup: $backup_dir"
echo "Restore: $PROJECT_ROOT/scripts/restore-main-user-test.sh"
