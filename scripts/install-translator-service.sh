#!/usr/bin/env bash
# Usage: install-translator-service.sh PREFIX [--no-start]
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 || ( $# -eq 2 && "$2" != --no-start ) ]]; then
    echo "Usage: $0 PREFIX [--no-start]" >&2
    exit 2
fi
prefix="$(cd "$1" && pwd)"
launcher="$prefix/bin/yiban-translator"
example="$prefix/share/yiban-ime/config/bilingualcontext.conf"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="${FCITX_CONFIG_HOME:-$config_home/fcitx5}/conf"
service_dir="$config_home/systemd/user"
service="$service_dir/bilingual-ime-translator.service"
test -x "$launcher"
test -f "$example"
mkdir -p "$config_dir" "$service_dir"
if [[ ! -e "$config_dir/bilingualcontext.conf" ]]; then
    install -m 0644 "$example" "$config_dir/bilingualcontext.conf"
fi
"$launcher" --config "$config_dir/bilingualcontext.conf" --check-config
if [[ -e "$service" ]]; then
    cp -p "$service" "$service.backup-$(date +%Y%m%d-%H%M%S)"
fi
# systemd quoting, including literal percent specifiers and unusual prefix paths.
python3 - "$launcher" "$config_dir/bilingualcontext.conf" "$service" <<'PY'
import sys
from pathlib import Path

def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('\n', '\\n') + '"'
launcher, config, service = sys.argv[1:]
Path(service).write_text(
    '[Unit]\nDescription=Yiban IME sentence translation service\n\n'
    '[Service]\nType=simple\nExecStart=' + quote(launcher) + ' --config ' + quote(config) + '\n'
    'Restart=on-failure\nRestartSec=3\n\n[Install]\nWantedBy=default.target\n'
)
PY
if [[ "${2:-}" != --no-start ]]; then
    systemctl --user daemon-reload
    systemctl --user enable bilingual-ime-translator.service
    systemctl --user restart bilingual-ime-translator.service
fi
echo "Service: $service"
echo "Configuration preserved: $config_dir/bilingualcontext.conf"
