/* Runtime adapter; actual kernel allocation lifetime, no title calls. djultra64. */
#ifndef VISUAL_PB_LINEAGE_RUNTIME_H
#define VISUAL_PB_LINEAGE_RUNTIME_H
#include "visual_pb_lineage.h"
typedef struct {void*user;int(*owner)(void*);} VplRuntime;
/* Owner predicate must accept only actual serialized emitter/decoder owner
 * boundaries; OFF performs no memory/epoch calls. Context outlives ledger. */
VplReader vpl_runtime_reader(VplRuntime*,int enabled);
#endif
