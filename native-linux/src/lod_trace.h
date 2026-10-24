/* Bounded Game LOD selection observations. Own work: djultra64. */
#ifndef TOPSPIN_LOD_TRACE_H
#define TOPSPIN_LOD_TRACE_H
#include <stdint.h>
#define TOPSPIN_LOD_TIERS 16u
typedef struct TopspinLodSample {
 uint64_t wall_ns;
 uint32_t thread,caller,owner,position[3],forced,thresholds,meshes,count,selected;
 uint32_t mesh[TOPSPIN_LOD_TIERS],threshold[TOPSPIN_LOD_TIERS];
} TopspinLodSample;
/* Call once before guest threads. Unset/empty disables. Enabled path must be
 * absolute. No file is opened until main-thread poll outside the guest token. */
int topspin_lod_trace_init(void);
int topspin_lod_trace_enabled(void);
void topspin_lod_trace_sample(const TopspinLodSample *sample);
/* Main-thread only. Append newly published immutable samples. force closes the
 * writer permanently. Existing files/symlinks are never replaced; mode0600. */
int topspin_lod_trace_poll(int force);
#endif
