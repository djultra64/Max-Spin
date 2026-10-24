/* Private original model checkpoint. Own work: djultra64. */
#ifndef TOPSPIN_PLAYERCREATOR_MODEL_CHECKPOINT_H
#define TOPSPIN_PLAYERCREATOR_MODEL_CHECKPOINT_H
#include <stdint.h>
/* Startup, same canonical save root as VFS. Exact coherence+checkpoint1 opts in.
 * Exact TOPSPIN_CREATOR_MODEL_OBSERVE=1 takes only a read-only graph snapshot,
 * with no ADN IO/serializer/loader; restore in this mode observes only.
 * OFF/Shop begin returns1 without IO. Enabled Creator begin0 means original
 * Save MUST NOT run. Restore after operation rollback/leave, before UI recovery.
 * Full original ADN only; no Career success/flag synthesis, no visibility writes.
 * CPU and borrowed stack restored. PRNG is checked, never rolled back. */
int topspin_creator_model_checkpoint_configure(const char*save_root);
int topspin_creator_model_checkpoint_begin(unsigned kind,uint32_t owner);
int topspin_creator_model_checkpoint_restore(unsigned kind,uint32_t owner);
void topspin_creator_model_checkpoint_discard(void);
/* Read-only bounded existing graph observation; no original callbacks. */
void topspin_creator_model_checkpoint_observe(const char*phase);
#endif
