#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PREFIX="${YIBAN_FCITX_PREFIX:-$PROJECT_ROOT/.local-env/prefix}"
EXTRA_LIBS="${YIBAN_EXTRA_LIBRARY_PATH:-}"
if [[ -z "$EXTRA_LIBS" && "$PREFIX" == "$PROJECT_ROOT/.local-env/prefix" ]]; then
    for directory in "$PROJECT_ROOT"/.local-env/sysroot/usr/lib/*-linux-gnu; do
        [[ -d "$directory" ]] || continue
        EXTRA_LIBS="${EXTRA_LIBS:+$EXTRA_LIBS:}$directory"
    done
fi
RIME_USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/rime"
RIME_SHARED_DIR="${RIME_SHARED_DIR:-$PREFIX/share/rime-data}"
RIME_DEPLOYER="${RIME_DEPLOYER:-$PREFIX/bin/rime_deployer}"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/bilingual-ime/rime-ice"
SOURCE_URL="https://github.com/iDvel/rime-ice.git"
SOURCE_REF="${RIME_ICE_REF:-main}"
SKIP_REDEPLOY=0

usage() {
    cat <<'EOF'
Usage: update-rime-ice-dictionary.sh [--skip-redeploy]

Update the Chinese candidate vocabulary used by pinyin_simp to the latest
Rime Ice dictionary. Set RIME_ICE_REF to a tag or commit to install a pinned
revision. The input schema and pinyin_simp user database are left unchanged.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-redeploy)
            SKIP_REDEPLOY=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
    shift
done

test -x "$RIME_DEPLOYER"
test -d "$RIME_SHARED_DIR"

mkdir -p "$(dirname "$CACHE_DIR")"
if [[ ! -d "$CACHE_DIR/.git" ]]; then
    git clone --filter=blob:none --no-checkout "$SOURCE_URL" "$CACHE_DIR"
fi

if [[ "$(git -C "$CACHE_DIR" remote get-url origin)" != "$SOURCE_URL" ]]; then
    echo "Unexpected Rime Ice cache origin: $CACHE_DIR" >&2
    exit 1
fi

git -C "$CACHE_DIR" sparse-checkout init --cone
git -C "$CACHE_DIR" sparse-checkout set cn_dicts
git -C "$CACHE_DIR" fetch --depth 1 origin "$SOURCE_REF"
git -C "$CACHE_DIR" checkout --detach --force FETCH_HEAD
revision="$(git -C "$CACHE_DIR" rev-parse --verify HEAD)"

required_files=(
    rime_ice.dict.yaml
    LICENSE
    cn_dicts/8105.dict.yaml
    cn_dicts/base.dict.yaml
    cn_dicts/ext.dict.yaml
    cn_dicts/tencent.dict.yaml
    cn_dicts/others.dict.yaml
)
for relative_path in "${required_files[@]}"; do
    if [[ ! -f "$CACHE_DIR/$relative_path" ]]; then
        echo "Rime Ice revision is missing $relative_path" >&2
        exit 1
    fi
done

mkdir -p "$RIME_USER_DIR/cn_dicts"
install -m 0644 "$CACHE_DIR/rime_ice.dict.yaml" \
    "$RIME_USER_DIR/rime_ice.dict.yaml"
for dictionary in 8105 base ext tencent others; do
    install -m 0644 "$CACHE_DIR/cn_dicts/$dictionary.dict.yaml" \
        "$RIME_USER_DIR/cn_dicts/$dictionary.dict.yaml"
done
install -m 0644 "$CACHE_DIR/LICENSE" "$RIME_USER_DIR/RIME-ICE-LICENSE"
install -m 0644 "$PROJECT_ROOT/config/rime/pinyin_simp.custom.yaml" \
    "$RIME_USER_DIR/pinyin_simp.custom.yaml"
printf '%s\n' "$revision" >"$RIME_USER_DIR/rime-ice.version"

if [[ "$SKIP_REDEPLOY" == 0 ]]; then
    LD_LIBRARY_PATH="$PREFIX/lib${EXTRA_LIBS:+:$EXTRA_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$RIME_DEPLOYER" --build \
        "$RIME_USER_DIR" "$RIME_SHARED_DIR" "$RIME_USER_DIR/build"
fi

echo "Rime Ice dictionary installed: $revision"
echo "Schema retained: pinyin_simp"
echo "Dictionary selected: rime_ice"
