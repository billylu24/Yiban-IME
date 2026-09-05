#!/usr/bin/env bash
# A desktop shell does not inherit a private Fcitx process's XDG_DATA_DIRS.
# Usage: install-user-icons.sh PREFIX
set -euo pipefail
if [[ $# != 1 ]]; then
    echo "Usage: $0 PREFIX" >&2
    exit 2
fi
prefix="$(cd "$1" && pwd)"
source_icons="$prefix/share/icons/hicolor"
user_icons="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor"
test -d "$source_icons"
count=0
for source in "$source_icons"/*/apps/fcitx-yiban.png; do
    [[ -f "$source" ]] || continue
    relative="${source#"$source_icons"/}"
    target="$user_icons/$relative"
    mkdir -p "$(dirname "$target")"
    if [[ "$(readlink -f "$source")" != "$(readlink -f "$target")" ]]; then
        install -m 0644 "$source" "$target"
    fi
    count=$((count + 1))
done
if [[ "$count" == 0 ]]; then
    echo "No Yiban icons found under $source_icons; install with YIBAN_BRAND_RIME=ON first." >&2
    exit 1
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$user_icons"
fi
echo "Installed $count Yiban icons in $user_icons"
echo "Restart Fcitx to refresh the tray item."
