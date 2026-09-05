#!/usr/bin/env bash

set -euo pipefail

CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
CONFIG_DIR="$CONFIG_HOME/fcitx5"
AUTOSTART_DIR="$CONFIG_HOME/autostart"
STATE_ROOT="${XDG_STATE_HOME:-$HOME/.local/state}/giaok-keyboard"
ACTIVE_STATE="$STATE_ROOT/active-main-user-test"
LAUNCHER="$HOME/.local/bin/fcitx5-giaok-5.1.22"
TRANSLATOR_LAUNCHER="$HOME/.local/bin/bilingual-ime-translator"
TRANSLATOR_SERVICE="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/bilingual-ime-translator.service"
PROFILE="$CONFIG_DIR/profile"
BILINGUAL_CONFIG="$CONFIG_DIR/conf/bilingualcontext.conf"
AUTOSTART="$AUTOSTART_DIR/org.fcitx.Fcitx5.desktop"
RIME_USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/rime"
TRANSLATOR_PID="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/bilingual-ime/translator.pid"

if [[ ! -f "$ACTIVE_STATE" ]]; then
    echo "No active Giaok main-environment test was found."
    exit 0
fi

# This state file is generated locally by the paired installer and contains
# only absolute paths and 0/1 flags.
backup_dir=""
profile_existed=0
bilingual_config_existed=0
autostart_existed=0
launcher_existed=0
translator_launcher_existed=0
translator_service_existed=0
rime_dir_existed=0
# shellcheck disable=SC1090
source "$ACTIVE_STATE"

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

systemctl --user disable --now bilingual-ime-translator.service >/dev/null 2>&1 || true

if [[ -f "$TRANSLATOR_PID" ]]; then
    translator_pid="$(<"$TRANSLATOR_PID")"
    if [[ -r "/proc/$translator_pid/cmdline" ]] &&
        tr '\0' ' ' <"/proc/$translator_pid/cmdline" | grep -q 'translator/daemon.py'; then
        kill "$translator_pid" 2>/dev/null || true
    fi
fi

if [[ "${translator_service_existed:-0}" == 1 ]]; then
    install -m 0644 "$backup_dir/translator.service" "$TRANSLATOR_SERVICE"
else
    unlink "$TRANSLATOR_SERVICE" 2>/dev/null || true
fi
if [[ "${translator_launcher_existed:-0}" == 1 ]]; then
    install -m 0755 "$backup_dir/translator-launcher" "$TRANSLATOR_LAUNCHER"
else
    unlink "$TRANSLATOR_LAUNCHER" 2>/dev/null || true
fi
systemctl --user daemon-reload

if [[ "$profile_existed" == 1 ]]; then
    install -m 0644 "$backup_dir/profile" "$PROFILE"
else
    rm -f "$PROFILE"
fi

if [[ "${bilingual_config_existed:-0}" == 1 ]]; then
    install -m 0644 "$backup_dir/bilingualcontext.conf" "$BILINGUAL_CONFIG"
else
    rm -f "$BILINGUAL_CONFIG"
fi

if [[ "$autostart_existed" == 1 ]]; then
    install -m 0644 "$backup_dir/autostart.desktop" "$AUTOSTART"
else
    rm -f "$AUTOSTART"
fi

if [[ "$launcher_existed" == 1 ]]; then
    install -m 0755 "$backup_dir/launcher" "$LAUNCHER"
else
    rm -f "$LAUNCHER"
fi

if [[ -d "$RIME_USER_DIR" ]]; then
    mv "$RIME_USER_DIR" "$backup_dir/test-session-rime"
fi
if [[ "${rime_dir_existed:-0}" == 1 ]]; then
    mkdir -p "$(dirname "$RIME_USER_DIR")"
    cp -a "$backup_dir/rime-user-dir" "$RIME_USER_DIR"
fi

rm -f "$ACTIVE_STATE"
/usr/bin/fcitx5 -d

echo "Restored the previous Fcitx profile and autostart entry."
echo "Active runtime: $(/usr/bin/fcitx5 --version)"
echo "Backup retained at: $backup_dir"
