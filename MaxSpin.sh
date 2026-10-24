#!/usr/bin/env bash
# Max Spin launcher. Copyright (c) 2026 djultra64.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ -x "$root/native-linux/manual-builds/latest/topspin_window_host" ]]; then
    export TOPSPIN_NATIVE_BUILD_DIR="$(readlink -f -- "$root/native-linux/manual-builds/latest")"
elif [[ -x "$root/bin/topspin_window_host" ]]; then
    export TOPSPIN_NATIVE_BUILD_DIR="$root/bin"
elif [[ -x "$root/build/topspin_window_host" ]]; then
    export TOPSPIN_NATIVE_BUILD_DIR="$root/build"
fi
if [[ ${1:-} == --play ]]; then shift; fi
exec "$root/native-linux/play.sh" "$@"
