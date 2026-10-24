/* Diagnostic captures of the actual rendered backbuffer. Own work: djultra64. */
#ifndef TOPSPIN_FRAME_CAPTURE_H
#define TOPSPIN_FRAME_CAPTURE_H
#include <stdint.h>
#include "d3d8_xbox.h"
/* GPU executor only, immediately after a successful Present. elapsed_ns is
 * SDL_GetTicksNS() since SDL initialization, matching scripted input timing.
 * Opt in with TOPSPIN_CAPTURE_PREFIX; TOPSPIN_CAPTURE_SECONDS defaults to
 * "25,50,80". Names are PREFIX-025000ms.bmp, PREFIX-050000ms.bmp, etc.
 * TOPSPIN_CAPTURE_FRAMES, when present, takes priority over seconds. It must
 * contain1..16 comma-separated decimal integers1..10000000. Values are sorted
 * and deduplicated. Names are PREFIX-frame00001500.bmp. Missed frames are
 * skipped (never substituted with a later image). Frame is the host successful
 * Present counter passed by the caller, not a guest/PB draw counter.
 * Optional TOPSPIN_CAPTURE_RT_ADDRESS: exactly8 hex digits, no0x prefix.
 * Each scheduled capture additionally writes PREFIX-frameNNNNNNNN-rtXXXXXXXX
 * .rgba (unmodified RGBA8 including alpha) and .json (extent, FNV1a64,
 * alpha256/color16bin histograms), using live GPU readback. Guest extent
 * is limited to2048x2048/16MiB. Failures emit diagnostics, never black images.
 * Optional POSIX TOPSPIN_CAPTURE_REQUEST_FILE: absolute private0600 owned
 * regular file, opened NOFOLLOW; at most16 bytes, positive canonical decimal
 * counter with optional LF. Increasing counters request independent captures
 * named PREFIX-requestNNNNNNNN.bmp, maximum16 attempts per child. Duplicate
 * or decreasing counters do not recapture. Existing valid startup counter
 * establishes a baseline, preventing stale requests across handoff. An empty,
 * zero or absent initial file arms the first later positive request. Use
 * atomic rename of a private0600 file; transient empty/invalid reads are ignored.
 * No additional I/O/API calls when this option is unset. Requests do not
 * trigger extra RT/history captures and do not consume the scheduled budget.
 * At most 16 scheduled captures; no window activation or overlays. */
void topspin_frame_capture_after_present(IDirect3DDevice8 *device,
                                         uint64_t elapsed_ns, uint64_t frame);
#endif
