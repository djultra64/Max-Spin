/* Exact original CPU compressor source seam, proposed. djultra64. */
#ifndef TOPSPIN_MATERIAL_SOURCE_LOCK_H
#define TOPSPIN_MATERIAL_SOURCE_LOCK_H
#include "material_coherence_bridge.h"
typedef struct {McLock lock;uint32_t entry,locked_rect,descriptor_address,descriptor[7];int candidate;} McSourceLock;
int mc_source_lock_configure(McProducerCapture);
void mc_source_lock_begin(McSourceLock*);
void mc_source_lock_end(McSourceLock*);
int mc_source_lock_failed(void);
/* Only the authenticated outer Save wrapper begins a new recoverable scope. */
void mc_source_lock_operation_begin(void);
#endif
