#!/usr/bin/env bash
# Portable desktop entry point. Own work: djultra64.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ ! -x "$root/portable-runtime/MaxSpin-Setup" ]]; then
    printf '%s\n' 'El paquete de Max Spin está incompleto. Vuelve a extraer el paquete Linux completo.' >&2
    exit 2
fi
exec "$root/portable-runtime/MaxSpin-Setup" "$@"
