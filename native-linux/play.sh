#!/usr/bin/env bash
# Manual play, without scripted input or diagnostic timeouts. Own work: djultra64.
set -euo pipefail
probe_dir=$(cd -- "$(dirname -- "$0")" && pwd -P)
# Prefer the complete persistent-window snapshot once published. Resolve it once
# so a later candidate cannot change modules halfway through a running session.
if [[ -z ${TOPSPIN_NATIVE_BUILD_DIR:-} && -x "$probe_dir/manual-builds/latest/topspin_window_host" ]]; then
    export TOPSPIN_NATIVE_BUILD_DIR="$(readlink -f -- "$probe_dir/manual-builds/latest")"
fi
if [[ -n ${TOPSPIN_NATIVE_BUILD_DIR:-} && -x "$TOPSPIN_NATIVE_BUILD_DIR/topspin_window_host" ]]; then
    export TOPSPIN_PERSISTENT_WINDOW=1
    export TOPSPIN_MENU_PROMPTS_DIR="$probe_dir/cache/menu-prompts-v2"
    # Older immutable snapshots keep using v2; new binaries select their own
    # verified per-image v3 resources through this additional root.
    export TOPSPIN_PROMPTS_ROOT="$probe_dir/cache/controller-prompts-v3"
    export TOPSPIN_MENU_PC_DIR="$probe_dir/cache/menu-pc-v2"
    # Only the launcher's own default directory is created. An explicit
    # preference path belongs to its caller and must never be replaced here.
    if [[ ${TOPSPIN_GRAPHICS_INI+x} != x ]]; then
        preferences_dir="$probe_dir/cache/pc-preferences"
        if ! mkdir -p -m 700 -- "$preferences_dir"; then
            printf '[PLAY] Cannot create default graphics preferences directory: %s\n' "$preferences_dir" >&2
            exit 2
        fi
        export TOPSPIN_GRAPHICS_INI="$preferences_dir/graphics.ini"
    elif [[ -z $TOPSPIN_GRAPHICS_INI ]]; then
        printf '[PLAY] TOPSPIN_GRAPHICS_INI must be a nonempty explicit path.\n' >&2
        exit 2
    fi
fi
# Normal manual play uses the same quiet runtime policy as clean performance
# runs. Observations are an explicit opt-in, never a side effect of one window.
export TOPSPIN_MANUAL_DIAGNOSTICS=${TOPSPIN_MANUAL_DIAGNOSTICS:-0}
if [[ $TOPSPIN_MANUAL_DIAGNOSTICS == 1 ]]; then
    export RECOMP_RUNTIME_QUIET=${RECOMP_RUNTIME_QUIET:-0}
else
    export RECOMP_RUNTIME_QUIET=${RECOMP_RUNTIME_QUIET:-1}
fi
export RECOMP_WATCHDOG_SECS=0
unset RECOMP_PAD_SCRIPT RECOMP_PAD_SCRIPT_FORMAT RECOMP_PAD_LIVE
unset RECOMP_PAD_PRESS RECOMP_PAD_SCRIPT_KEYS RECOMP_PAD_SCRIPT_VIRTUAL
unset RECOMP_INPUT_VIRTUAL_PADS RECOMP_INPUT_VIRTUAL_LATE_MS
unset TOPSPIN_TEST_SCRIPT_INITIAL_ONLY
unset TOPSPIN_PRIVATE_APPEARANCE_IMPORT TOPSPIN_PRIVATE_APPEARANCE_DIAGNOSTIC
unset TOPSPIN_PRIVATE_APPEARANCE_CANONICAL_IMPORT TOPSPIN_CREATOR_CASE_CAPTURE
unset TOPSPIN_CREATOR_MODEL_CHECKPOINT TOPSPIN_CREATOR_MODEL_OBSERVE
unset TOPSPIN_CAPTURE_PREFIX TOPSPIN_BLACK_WATCH_PREFIX
unset TOPSPIN_PHYSICS_TRACE TOPSPIN_RENDER_TRACE TOPSPIN_INTERVAL_EVENTS
unset TOPSPIN_TIMING_TRACE TOPSPIN_CPU_PROFILE TOPSPIN_REPLAY_SAMPLE_PREFIX
unset RECOMP_POINT_CAPTURE_PREFIX RECOMP_STADIUM_CAPTURE_PREFIX RECOMP_PB_HISTORY
unset RECOMP_PB_BATCH_DUMP TOPSPIN_VIDEO_TRACE
export RECOMP_PAD_HOST=1
exec "$probe_dir/run.sh" "${1:-menu}"
