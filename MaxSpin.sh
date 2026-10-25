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
# File-manager launches have no terminal. Preserve early errors and explain
# missing first-start requirements before creating a game window. djultra64.
show_launcher_error() {
    local message="$1"
    printf '%s\n' "$message" >&2
    if [[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]]; then
        if command -v zenity >/dev/null 2>&1; then
            if zenity --error --title="Max Spin" --width=600 --no-markup --text="$message"; then return 0; fi
        fi
        if command -v kdialog >/dev/null 2>&1; then
            if kdialog --error "$message" --title "Max Spin"; then return 0; fi
        fi
        # Standard desktop text opener is a visible fallback when optional
        # dialog helpers are absent or unavailable. Keep the file for reading.
        local message_file
        if message_file=$(mktemp --suffix=.txt); then
            printf '%s\n' "$message" > "$message_file"
            if command -v xdg-open >/dev/null 2>&1; then
                if xdg-open "$message_file"; then return 0; fi
            fi
            if command -v gio >/dev/null 2>&1; then
                gio open "$message_file" || true
            fi
        fi
    fi
}
launcher_log_dir="$root/native-linux/logs"
if ! mkdir -p -- "$launcher_log_dir"; then
    show_launcher_error "No se puede escribir en la carpeta de Max Spin. Extrae el paquete completo en una carpeta donde puedas guardar archivos."
    exit 2
fi
if ! launcher_log=$(mktemp "$launcher_log_dir/launcher-XXXXXXXX.log"); then
    show_launcher_error "No se puede crear el registro de inicio en $launcher_log_dir."
    exit 2
fi
launch_application() {
    if ! command -v python3 >/dev/null 2>&1; then
        printf 'Falta Python 3. Instálalo para preparar e iniciar Max Spin.\n'
        return 2
    fi
    python3 "$root/native-linux/tools/startup_check.py" --root "$root" --build-dir "${TOPSPIN_NATIVE_BUILD_DIR:-$root/build}" || return $?
    "$root/native-linux/play.sh" "$@"
}
if launch_application "$@" 2>&1 | tee "$launcher_log"; then
    exit 0
else
    launcher_status=${PIPESTATUS[0]}
fi
if [[ $launcher_status == 130 || $launcher_status == 143 ]]; then
    exit "$launcher_status"
fi
if [[ $launcher_status == 0 ]]; then launcher_status=1; fi
launcher_detail=$(tail -n 24 -- "$launcher_log")
show_launcher_error "Max Spin no pudo iniciar.

$launcher_detail

Registro de inicio: $launcher_log"
exit "$launcher_status"
