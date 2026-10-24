/* Proposed original resource/PB producer journal hooks. djultra64. */
#ifndef TOPSPIN_MATERIAL_TRACKING_H
#define TOPSPIN_MATERIAL_TRACKING_H
/* Startup, before guest. DefaultOFF exact COHERENCE=1. Backend journal and
 * executor93 must already be configured. This does not enable Save/copy. */
int topspin_creator_material_tracking_configure(void);
int topspin_creator_material_tracking_failed(void);
#include <stdint.h>
void topspin_creator_material_wrap_observe(uint32_t device,uint32_t actual_jump,uint32_t actual_word,uint32_t actual_reset);
#endif
