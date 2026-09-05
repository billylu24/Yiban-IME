#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATE_FILE="${XDG_STATE_HOME:-$HOME/.local/state}/giaok-keyboard/active-main-user-test"

if [[ -f "$STATE_FILE" ]]; then
    project_root=""
    # shellcheck disable=SC1090
    source "$STATE_FILE"
    PROJECT_ROOT="$project_root"
else
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

PYTHON="${BILINGUAL_TRANSLATOR_PYTHON:-python3}"

exec "$PYTHON" "$PROJECT_ROOT/translator/daemon.py" "$@"
