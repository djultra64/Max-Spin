/* Exact original store once; preserve complete represented CPU. djultra64. */
#include "visual_pb_hooks.h"
#define RECOMP_GENERATED_CODE
#include "actor_binding_cpu_guard.h"
#include <stdint.h>
extern VpliBridge*vpli_hooks_bridge(void);
static _Thread_local unsigned observation_depth,observation_broken;
int topspin_pb_observe_scope_enter(void){if(observation_broken||observation_depth==16){observation_broken=1;return 0;}observation_depth++;return 1;}
void topspin_pb_observe_scope_leave(void){if(!observation_depth){observation_broken=1;return;}observation_depth--;}
int topspin_pb_observe_scope_active(void){return !observation_broken&&observation_depth!=0;}
static void original_store(void*pointer,uint32_t va,uint32_t value){(void)va;*(volatile uint32_t*)pointer=value;}
void topspin_pb_lineage_put_store(uint32_t address,uint32_t value){
 VpliBridge*b=vpli_hooks_bridge();void*pointer=(void*)XBOX_PTR(address);
 if(!b){original_store(pointer,address,value);return;}
 VabGuestCpu saved;vab_cpu_save(&saved);int scoped=topspin_pb_observe_scope_enter();vpli_store_put(b,address,value,original_store,pointer);if(scoped)topspin_pb_observe_scope_leave();vab_cpu_restore(&saved);
}
void topspin_pb_lineage_wrap(uint32_t at,uint32_t word,uint32_t reset){
 VpliBridge*b=vpli_hooks_bridge();if(!b)return;VabGuestCpu saved;vab_cpu_save(&saved);int scoped=topspin_pb_observe_scope_enter();(void)vpli_observe_wrap(b,at,word,reset);if(scoped)topspin_pb_observe_scope_leave();vab_cpu_restore(&saved);
}

int topspin_pb_lineage_stage_scene(const VpliSceneTag*tag){VpliBridge*b=vpli_hooks_bridge();if(!b)return 0;VabGuestCpu saved;vab_cpu_save(&saved);int scoped=topspin_pb_observe_scope_enter();int ok=vpli_stage_scene(b,tag);if(scoped)topspin_pb_observe_scope_leave();vab_cpu_restore(&saved);return ok;}
