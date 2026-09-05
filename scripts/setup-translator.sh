#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Uses the same configuration and URL as the daemon, including remote Ollama.
exec "${BILINGUAL_TRANSLATOR_PYTHON:-python3}" "$SCRIPT_DIR/../translator/daemon.py" --prepare-model "$@"
