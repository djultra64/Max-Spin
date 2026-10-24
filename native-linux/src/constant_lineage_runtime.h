/* Actual consumed method/VSH writer -> owned SOURCE evidence. djultra64. */
#ifndef TOPSPIN_CONSTANT_LINEAGE_RUNTIME_H
#define TOPSPIN_CONSTANT_LINEAGE_RUNTIME_H
#include "visual_source_archive.h"
/* Startup once before GPU workers. Called only by opt-in scene recorder. */
int topspin_constant_lineage_init(void);
/* Actual SOURCE on original GPU owner. Missing evidence remains discrete. */
int topspin_constant_lineage_capture(const VpliPacket*,const VfpSource*,uint64_t actual_device_epoch);
int topspin_constant_lineage_lookup(const VsjFrame*,uint32_t,const VclSnapshot**);
/* After immutable HOST bindings have copied any evidence they need. */
void topspin_constant_lineage_retire(uint64_t actual_frame);
#endif
