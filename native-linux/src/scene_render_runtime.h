/* Explicit recorder glue, no output or simulation service. djultra64. */
#ifndef TOPSPIN_SCENE_RENDER_RUNTIME_H
#define TOPSPIN_SCENE_RENDER_RUNTIME_H
#include "visual_pb_hooks.h"
int topspin_scene_render_init(void);
int topspin_scene_render_lineage(void*,const VpliPacket*,const VfpSource*);
/* May run under lineage mutex on producer or consumer: atomic-only. */
void topspin_scene_render_lineage_fault(unsigned);
#endif
