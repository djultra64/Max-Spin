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
/* Guest CPU thread notification only; no guest pointers, readback or file IO.
 * POSIX diagnostic, defaultOFF. TOPSPIN_REPLAY_RENDER_CAPTURE must equal "1"
 * and TOPSPIN_CAPTURE_PREFIX must be an absolute private output prefix.
 * Requests at replay-view counts1,4,16,64,128,256 coalesce until the next
 * successful Present. Counts saturate at256; at most6 capture attempts per
 * child. GPU executor captures PREFIX-replay-viewNNNNNN-frameNNNNNNNN.bmp
 * with private0600 exclusive creation (existing evidence is never replaced),
 * then directly dumps that completed frame's PB history to the runtime log.
 * Set RECOMP_PB_HISTORY=1 at process startup to record that history. Coalesced
 * thresholds are logged as a mask; the filename uses the highest threshold.
 * No dependency on TOPSPIN_CAPTURE_PB_HISTORY or the timed capture budget. */
void topspin_frame_capture_replay_view(void);
/* Additional POSIX live crowd-atlas diagnostic, defaultOFF. Set exactly
 * TOPSPIN_CROWD_CAPTURE=1 and TOPSPIN_CROWD_CAPTURE_PREFIX to an absolute
 * prefix inside an existing owned private directory (no group/other access).
 * No address, pixel or stadium selection is required: the backend enumerates
 * current 64x128 RGB565 color targets and readback copies actual GPU images.
 * TOPSPIN_CROWD_CAPTURE_MATCH_HAIR=1 captures only successful completed frames
 * reported by nv2a_pb_hair_capture_completed_frame, writing its payload flip
 * alongside the host Present frame. Hair and crowd share one paired BMP.
 * Otherwise MIN_FRAME (default1000) and INTERVAL_FRAMES (default60), prefixed
 * TOPSPIN_CROWD_CAPTURE_, choose an independent cadence after live targets
 * first exist. Positive canonical decimal values are bounded to10000000.
 * Limits per worker:12 snapshots,64 atlases/snapshot,768 atlas readbacks,
 * 4096 metadata queries,400MiB paired BMP bytes (64MiB/image, max4096x4096),
 * 25MiB atlas data. All new files are0600/exclusive; existing evidence is
 * preserved. JSON records all retained addresses, read/write failures, source
 * raster extents, FNV1a64, truncation, backbuffer result and exact frame pair.
 * Atlas truncation is explicit; failed readbacks never produce black images.
 * Captures also dump the same completed Present's PB history to the log.
 * This option is independent of existing replay/request/timed capture budgets;
 * disable those other screenshot schedules when using its shared BMP budget.
 * Synchronous readbacks stall rendering; these are diagnostic samples, not
 * performance measurements or visual acceptance of crowd rendering. */
#endif
