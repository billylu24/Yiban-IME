#!/usr/bin/env bash

# Source this file from the repository root:
#   source scripts/activate-local-env.sh

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "source this script instead of executing it" >&2
    exit 2
fi

BILINGUAL_IME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BILINGUAL_IME_ENV="$BILINGUAL_IME_ROOT/.local-env"

export BILINGUAL_IME_ROOT BILINGUAL_IME_ENV
export PATH="$BILINGUAL_IME_ENV/prefix/bin:$PATH"
export CMAKE_PREFIX_PATH="$BILINGUAL_IME_ENV/prefix:$BILINGUAL_IME_ENV/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_PATH="$BILINGUAL_IME_ENV/prefix/lib/pkgconfig:$BILINGUAL_IME_ENV/prefix/lib/x86_64-linux-gnu/pkgconfig:$BILINGUAL_IME_ENV/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$BILINGUAL_IME_ENV/sysroot/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$BILINGUAL_IME_ENV/prefix/lib:$BILINGUAL_IME_ENV/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_CONFIG_HOME="$BILINGUAL_IME_ENV/xdg-config"
export XDG_DATA_HOME="$BILINGUAL_IME_ENV/xdg-data"
export XDG_CACHE_HOME="$BILINGUAL_IME_ENV/xdg-cache"
export XDG_RUNTIME_DIR="$BILINGUAL_IME_ENV/runtime"
export XDG_DATA_DIRS="$BILINGUAL_IME_ENV/prefix/share:/usr/local/share:/usr/share"

echo "Bilingual IME local environment: $BILINGUAL_IME_ENV"

