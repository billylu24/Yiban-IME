#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/activate-local-env.sh"

MODULE="$BILINGUAL_IME_ENV/prefix/lib/fcitx5/librime.so"
DICTIONARY="$BILINGUAL_IME_ENV/prefix/share/bilingual-ime/dictionary/base.tsv"
LOG="$BILINGUAL_IME_ENV/smoke-test.log"

mkdir -p \
    "$XDG_CONFIG_HOME/fcitx5" \
    "$XDG_DATA_HOME/fcitx5/rime" \
    "$XDG_CACHE_HOME" \
    "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
install -m 0644 "$BILINGUAL_IME_ROOT/config/smoke/profile" \
    "$XDG_CONFIG_HOME/fcitx5/profile"
install -m 0644 "$BILINGUAL_IME_ROOT/config/smoke/default.custom.yaml" \
    "$XDG_DATA_HOME/fcitx5/rime/default.custom.yaml"

test -x "$BILINGUAL_IME_ENV/prefix/bin/fcitx5"
test -f "$MODULE"
test -f "$DICTIONARY"

set +e
timeout 12s "$BILINGUAL_IME_ENV/prefix/bin/fcitx5" \
    -D -k \
    --disable all \
    --enable keyboard,rime,bilingualcontext \
    --verbose 'default=4,rime=5,bilingualcontext=5' >"$LOG" 2>&1
status=$?
set -e

if [[ $status -ne 0 && $status -ne 124 ]]; then
    echo "Fcitx smoke test exited unexpectedly ($status). See $LOG" >&2
    exit "$status"
fi

grep -Eq 'Starting fcitx5 5\.1\.22' "$LOG"
grep -Eq 'Loaded [0-9]+ bilingual word hints' "$LOG"
grep -Eq '] Loaded addon rime$' "$LOG"
grep -Eq '] Loaded addon bilingualcontext$' "$LOG"
grep -Eq "dictionary 'pinyin_simp' is ready|schema: pinyin_simp|loading config file .*pinyin_simp\.schema\.yaml" "$LOG"

if ldd "$MODULE" | grep -q 'not found'; then
    echo "The Rime addon has unresolved shared libraries." >&2
    exit 1
fi

echo "PASS: Fcitx5 5.1.22 loaded the patched Rime addon and bilingual dictionary."
echo "Log: $LOG"
