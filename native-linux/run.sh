#!/usr/bin/env bash
set -euo pipefail
probe_dir=$(cd -- "$(dirname -- "$0")" && pwd -P)
project_dir=$(dirname -- "$probe_dir")
image=${1:-game}
build_dir=${TOPSPIN_NATIVE_BUILD_DIR:-"$probe_dir/build"}
# Resolution uses the same data whitelist as CMake and the recompiler. No
# caller-supplied path or executable filename is interpolated into execution.
xbe=$(python3 "$probe_dir/pipeline.py" --resolve-image "$image")
if [[ ! -f "$project_dir/assets/$xbe" ]]; then
    printf 'Missing original image: %s\n' "$xbe" >&2; exit 2
fi
if [[ ! -x "$build_dir/topspin_$image" ]]; then
    printf 'Image is not built: %s (run pipeline.py %s and configure TOPSPIN_IMAGE=%s)\n' "$image" "$image" "$image" >&2; exit 2
fi
mkdir -p "$probe_dir/logs" "$probe_dir/saves"
mkdir -p "$probe_dir/cache"
export RECOMP_SHADER_CACHE=${RECOMP_SHADER_CACHE:-"$probe_dir/cache"}
export RECOMP_PB_EXEC=1 RECOMP_PB_D3D11=1 RECOMP_VBLANK=1 RECOMP_USB=1
if [[ ${TOPSPIN_MANUAL_DIAGNOSTICS:-0} == 1 ]]; then
    export RECOMP_STUB_LOG=1
    export RECOMP_RUNTIME_QUIET=${RECOMP_RUNTIME_QUIET:-0}
else
    unset RECOMP_STUB_LOG
    export RECOMP_RUNTIME_QUIET=${RECOMP_RUNTIME_QUIET:-1}
fi
# XDK 5558 builds/reinitializes KEVENTs inline on its stack (21C9B5).
# Synchronize the shadow with guest SignalState on every wait/set/reset.
export RECOMP_TITLE_KEVENTS=1
export RECOMP_CONTIG_RECLAIM=1
unset RECOMP_HEAP_RECLAIM
# XDK shadow passes clear a separate depth allocation. Sharing the main depth
# let later court draws overwrite the player. Keep explicit diagnostic opt-out.
if [[ ${TOPSPIN_SHADOW_SURFACES:-1} == 0 ]]; then
    unset RECOMP_GUEST_TARGETS
else
    export RECOMP_GUEST_TARGETS=1
fi
# Def Jam's fallback fence walkers assume its device layout and a wrap counter.
# XDK 5558 has a different ring and stores a byte distance at that offset.
# Keep the executor's actual semaphore releases; never guess completion from it.
export RECOMP_NO_FENCE_CATCHUP=1 RECOMP_NO_FENCE_FOLLOW=1
unset RECOMP_FENCE_ON_SUBMIT
export RECOMP_WATCHDOG_SECS=${RECOMP_WATCHDOG_SECS:-120}
# Always the user's real desktop. Do not set RECOMP_HEADLESS or start Xvfb.
unset RECOMP_HEADLESS
cd -- "$probe_dir"
set +e
log_path="$probe_dir/logs/$image-$(date +%Y%m%d-%H%M%S).log"
export TOPSPIN_FRAME_CSV="$log_path.frames.csv"
if [[ ${TOPSPIN_MANUAL_DIAGNOSTICS:-0} == 1 ]]; then
    # Bounded CPU observations only; no screenshots, readbacks or input scripts.
    export TOPSPIN_MENU_INPUT_TRACE="$log_path.menu-input.csv"
    export TOPSPIN_CAREER_TRACE="$log_path.career.csv"
    export TOPSPIN_VIDEO_TRACE=1 TOPSPIN_MOVIE_TRACE=1
else
    unset TOPSPIN_MENU_INPUT_TRACE TOPSPIN_CAREER_TRACE
    unset TOPSPIN_VIDEO_TRACE TOPSPIN_MOVIE_TRACE
fi
python3 "$probe_dir/launch.py" "$image" --build-dir "$build_dir" 2>&1 | tee "$log_path"
probe_status=${PIPESTATUS[0]}
printf '\nMax Spin exited with code %s.\n' "$probe_status"
exit "$probe_status"
