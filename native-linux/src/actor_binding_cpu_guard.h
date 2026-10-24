/* Guest register preservation around read-only host observations. djultra64. */
#ifndef TOPSPIN_ACTOR_BINDING_CPU_GUARD_H
#define TOPSPIN_ACTOR_BINDING_CPU_GUARD_H
#include "recomp_types.h"
#include <fenv.h>
typedef struct {
 uint32_t gpr[11];double fp[8];int top,cmp,df;uint16_t cw,cc;
 RecompXmm xmm[8];RecompMmx mm[8];ptrdiff_t memory_offset;
 int force_return;uint32_t code_lo,code_hi;fenv_t environment;
} VabGuestCpu;
void vab_cpu_save(VabGuestCpu*);
void vab_cpu_restore(const VabGuestCpu*);
/* One original call; probe cannot replace/skip it. Probe performs no guest
 * memory writes. Preservation concerns observations, NOT undoing the original.
 * No callback/frame classification or simulation-purity claim is made here. */
void vab_cpu_observe_call(void(*original)(void),void(*probe)(void*,unsigned),void*);
#endif
