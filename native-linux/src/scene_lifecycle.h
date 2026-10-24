/* Authenticated original Game constructor lifetimes. Own work: djultra64. */
#ifndef TOPSPIN_SCENE_LIFECYCLE_H
#define TOPSPIN_SCENE_LIFECYCLE_H
#include <stddef.h>
#include <stdint.h>
#define TSL_VERSION 2u
#define TSL_MAX_LIVE 64u
enum {TSL_APP=1,TSL_VIEW=2};
typedef struct {uint32_t version,app_va,view_va,reserved;uint64_t scene_id,view_id,authority_revision;uint32_t simulation_tick,simulation_time_bits;} TslSnapshot;
typedef int(*TslRead)(void*,uint32_t,void*,size_t);
/* Startup-only explicit0/1. Reconfiguration is rejected; OFF calls never read
 * guest RAM, allocate, query clocks or issue jobs. Reader runs on guest owner
 * thread only. This API neither queries a device epoch nor invents one. */
int tsl_configure(int enabled,TslRead,void*);
int tsl_enabled(void);
/* Host atomic load only, safe on the actual GPU executor. Zero is invalid.
 * Changes at constructor/destructor authority mutations immediately revoke
 * earlier snapshots without waiting for another render callback. */
uint64_t tsl_authority_revision(void);
/* Called exactly at actual original constructor entry and successful return.
 * begin retires any previous authority for this address before reuse. */
uint64_t tsl_ctor_begin(uint32_t kind,uint32_t va);
int tsl_ctor_end(uint32_t kind,uint32_t va,uint64_t ticket,uint32_t result_va);
/* Retire BEFORE the actual destructor; bytes/vtable can still look live. */
void tsl_dtor(uint32_t kind,uint32_t va);
/* Original EB620 entry only; never call on GPU/executor thread. */
int tsl_snapshot(uint32_t app_va,TslSnapshot*);
/* Pure host registry lookup, safe on GPU owner: exact live pinned manager,
 * scene/view constructor IDs and current authority. No reader callback or
 * guest-memory access. A successful lookup is not a lifetime lease. */
int tsl_live_owner(uint32_t owner_va,uint64_t scene_id,uint64_t view_id,uint64_t authority_revision);
#endif
