/* Original-UI recovery after a rejected whole Save. Own work: djultra64. */
#ifndef TOPSPIN_PLAYERCREATOR_SAVE_FAILURE_H
#define TOPSPIN_PLAYERCREATOR_SAVE_FAILURE_H
#include <stdint.h>
/* Calling guest thread only, after original cleanup and operation leave.
 * Authenticates current scene/class/name and cached original hub, then uses
 * original UI cleanup/transition. Never claims Save success or repairs tracking.
 * Failed guards log/reject; no guessed scene offsets, extra window or overlay.
 * CPU/FPU/SIMD/MMX and a borrowed16KiB guest stack region are restored. */
void topspin_creator_save_failure_recover(unsigned kind,uint32_t owner);
#endif
