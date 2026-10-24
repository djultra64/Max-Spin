/* Bounded native backbuffer diagnostics. Own work: djultra64. */
#ifndef TOPSPIN_BLACK_FRAME_WATCH_H
#define TOPSPIN_BLACK_FRAME_WATCH_H
#include <stdint.h>
#include "d3d8_xbox.h"
/* Call on the GPU executor after a successful presentation. Default off.
 * TOPSPIN_BLACK_WATCH_PREFIX enables one before/dark/after BMP triplet.
 * TOPSPIN_BLACK_WATCH_START/END are seconds, defaults 10/100. Readback and
 * disk writes are diagnostics and must be excluded from performance claims.
 * Returns 1 only on the dark transition, so the host may request a bounded
 * command-stream dump of the following frame. No display pixels are altered. */
int topspin_black_frame_watch_after_present(IDirect3DDevice8 *device,
                                           uint64_t elapsed_ns,uint64_t frame);
#endif
