/* Trusted original store seams; CPU effects preserved. Own work: djultra64. */
#include "visual_pb_hooks.h"
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "actor_binding_cpu_guard.h"
extern VpliBridge*vpli_hooks_bridge(void);
static void stores(void*u,uint32_t phase,uint32_t record){(void)u;
 if(phase==VPLI_FENCE_WAIT){MEM32(record+0x10)=0x40110;MEM32(record+0x14)=0;}
 else if(phase==VPLI_FENCE_TRAP){MEM32(record+0x18)=0x40100;MEM32(record+0x1c)=5;}
 else if(phase==VPLI_FENCE_RESTORE){MEM32(record+0x10)=0x40100;MEM32(record+0x14)=0;MEM32(record+0x18)=0x40100;MEM32(record+0x1c)=0;}
}
void topspin_pb_lineage_fence_birth(uint32_t record,uint32_t fence){
 VpliBridge*b=vpli_hooks_bridge();if(!b)return;VabGuestCpu saved;vab_cpu_save(&saved);int scoped=topspin_pb_observe_scope_enter();(void)vpli_fence_birth(b,record,fence);if(scoped)topspin_pb_observe_scope_leave();vab_cpu_restore(&saved);
}
void topspin_pb_lineage_fence_patch(uint32_t phase,uint32_t record,uint32_t fence){
 VpliBridge*b=vpli_hooks_bridge();if(!b){stores(NULL,phase,record);return;}VabGuestCpu saved;vab_cpu_save(&saved);int scoped=topspin_pb_observe_scope_enter();vpli_fence_patch(b,phase,record,fence,stores,NULL);if(scoped)topspin_pb_observe_scope_leave();vab_cpu_restore(&saved);
}
