/* Explicit actor-only sidecars; never whole-scene attestation. djultra64. */
#ifndef TOPSPIN_ACTOR_RUNTIME_H
#define TOPSPIN_ACTOR_RUNTIME_H
#include "actor_draw_binding.h"
#include "visual_scene_journal.h"
#define TAR_MAX_BYTES (8u*1024u*1024u)
#define TAR_MAX_BINDINGS 512u
#define TAR_MAX_SEALS 3u
enum {TAR_OK=1,TAR_OFF,TAR_INVALID,TAR_OWNER,TAR_BUDGET,TAR_MISSING,TAR_NOT_PROGRAM};
typedef struct TopspinActorSeal TopspinActorSeal;
/* Startup once before workers. Explicit OFF calls no dependency callbacks. */
int topspin_actor_runtime_init(int enabled);
/* Actual HOST GPU owner, before SOURCE archive retirement. Success owns copies
 * of proven bindings and per-command missing statuses, not *frame itself.
 * No actor input completeness/scene compatibility domain is claimed. */
int topspin_actor_finalize(const VsjFrame*,TopspinActorSeal**out);
int topspin_actor_seal_validate(const TopspinActorSeal*,const VsjFrame*);
/* Accessor borrows immutable bytes; validate the seal at actual use. */
const VadbBinding*topspin_actor_seal_binding(const TopspinActorSeal*,uint32_t command_index,int*status);
uint32_t topspin_actor_seal_count(const TopspinActorSeal*);
/* Only after actual consumers finish, or before any submission on failure. */
int topspin_actor_seal_release(TopspinActorSeal**);
/* After copying HOST sidecars (or rejecting the frame). Actual method130 source
 * interval is the retirement boundary; no guessed flip/time or CPU wait. */
int topspin_actor_retire_host(void);
#endif
