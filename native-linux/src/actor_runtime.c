/* Real original-hook CPU reads and deferred owned HOST proof. djultra64. */
#include "actor_runtime.h"
#include "actor_draw_hooks.h"
#include "actor_pb_adapter.h"
#include "actor_journal_binding.h"
#include "constant_lineage_runtime.h"
#include "scene_gpu_bridge.h"
#include "scene_lifecycle.h"
#include "guest_memory_arena_epoch.h"
#include "visual_pb_hooks.h"
#include "nv2a_executor_job.h"
#include "xbox_memory_layout.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct {int status;VadbBinding*binding;} Entry;
struct TopspinActorSeal {const VsjFrame*frame;VsjStamp stamp;size_t bytes;uint32_t commands,bound;Entry entries[];};
static VadbCollector collector;
static VpliBridge*bridge;
static int configured,on,busy;
static size_t charged;
static unsigned seals;
static int gpu(void){return nv2a_executor_is_owner();}
static int cpu_permission(void*u){(void)u;return topspin_pb_observe_scope_active()&&!gpu();}
static int gpu_permission(void*u){(void)u;return gpu();}
static int epoch(void*u,uint64_t*out){(void)u;if(!out||!cpu_permission(NULL))return 0;*out=xbox_GuestMemoryArenaEpoch();return *out!=0;}
static int read_guest(void*u,uint32_t va,void*out,size_t bytes){
 (void)u;if(!out||!bytes||!cpu_permission(NULL))return 0;
 uint64_t before=xbox_GuestMemoryArenaEpoch();if(!before)return 0;
 /* Top Spin graph inputs are logical RAM pointers. A broad guest VA map
  * is not proof that apertures, mirrors, sparse VMA or MMIO are readable.
  * Contiguous/physical aliases require their own allocation authority and
  * are deliberately unsupported by this actor reader. */
 if(g_xbox_total_ram!=0x4000000u&&g_xbox_total_ram!=0x8000000u)return 0;
 size_t limit=0x4000000u;
 if(g_xbox_map_size&&g_xbox_map_size<limit)limit=g_xbox_map_size;
 if(va<0x10000u||(uint64_t)va>=limit||bytes>limit-va)return 0;
 uintptr_t base=(uintptr_t)xbox_GetMemoryOffset();if(va>UINTPTR_MAX-base)return 0;
 uintptr_t at=base+va;if(bytes>UINTPTR_MAX-at)return 0;
 /* Original hook retains its original guest CPU turn throughout this bounded
  * memcpy; trusted scope is permission, not actor/scene identity. */
 memcpy(out,(const void*)at,bytes);
 return cpu_permission(NULL)&&xbox_GuestMemoryArenaEpoch()==before;
}
static int staged(void*u,VpliSceneTag*out){(void)u;return cpu_permission(NULL)&&vadb_vpli_staged(bridge,out);}
static int current(void*u,VpliSceneTag*out){
 (void)u;TopspinSceneHostStamp s;if(!out||!gpu()||!topspin_scene_gpu_current(&s))return 0;
 *out=(VpliSceneTag){s.version,s.simulation_tick,s.time_bits,s.reserved,s.scene,s.view,s.authority_revision,s.boundary_ns};return 1;
}
static int live_owner(void*u,uint32_t va,const VpliSceneTag*t){(void)u;return cpu_permission(NULL)&&t&&tsl_live_owner(va,t->scene,t->view,t->authority_revision);}
static int cursor(void*u,uint32_t put,VabSpan*out){
 (void)u;VplSpan s;if(!out||!cpu_permission(NULL)||!vpli_cursor(bridge,put,&s))return 0;
 *out=(VabSpan){s.identity,s.epoch,s.wrap_generation,s.begin,s.end};return 1;
}
int topspin_actor_runtime_init(int enabled){
 if(configured||(enabled!=0&&enabled!=1))return TAR_INVALID;
 configured=1;if(!enabled)return TAR_OFF;
 bridge=vpli_enabled_bridge;
 if(!bridge||!bridge->configured||!bridge->enabled||!tsl_enabled())return TAR_INVALID;
 VadbConfig c={1,{1,NULL,cpu_permission,epoch,read_guest},NULL,cpu_permission,gpu_permission,staged,current,live_owner,cursor};
 if(vadb_configure(&collector,&c)!=VADB_OK||!vadb_hooks_install(&collector))return TAR_INVALID;
 on=1;return TAR_OK;
}
static void discard(TopspinActorSeal*s){
 for(unsigned i=0;i<s->commands;i++)free(s->entries[i].binding);
 charged-=s->bytes;seals--;free(s);
}
static int frame_ok(const TopspinActorSeal*s,const VsjFrame*f){
 if(!s||s->frame!=f||!vsj_validate(f))return 0;
 const VsjStamp*t=vsj_stamp(f);return t&&!memcmp(t,&s->stamp,sizeof*t)&&vsj_command_count(f)==s->commands;
}
static int bindings_live(const TopspinActorSeal*s){
 uint64_t arena=xbox_GuestMemoryArenaEpoch();if(!arena)return 0;
 for(unsigned i=0;i<s->commands;i++){
  const VadbBinding*b=s->entries[i].binding;if(!b)continue;
  if(b->memory_epoch!=arena||b->stamp.device_epoch!=s->stamp.device_epoch||b->stamp.frame_serial!=s->stamp.frame_serial||!tsl_live_owner(b->owner,b->source.scene,b->source.view,b->source.authority_revision))return 0;
 }
 return 1;
}
int topspin_actor_finalize(const VsjFrame*f,TopspinActorSeal**out){
 if(!on)return TAR_OFF;if(!gpu()||busy)return TAR_OWNER;
 if(!out||*out||!f||!vsj_validate(f))return TAR_INVALID;
 uint32_t count=vsj_command_count(f);const VsjStamp*stamp=vsj_stamp(f);
 if(!stamp||!count||count>VSJ_MAX_COMMANDS)return TAR_INVALID;
 size_t bytes=sizeof(TopspinActorSeal)+(size_t)count*sizeof(Entry);
 if(seals==TAR_MAX_SEALS||bytes>TAR_MAX_BYTES-charged)return TAR_BUDGET;
 busy=1;TopspinActorSeal*s=calloc(1,bytes);if(!s){busy=0;return TAR_BUDGET;}
 s->frame=f;s->stamp=*stamp;s->commands=count;s->bytes=bytes;charged+=bytes;seals++;
 int result=TAR_OK;
 for(unsigned i=0;i<count;i++){
  const VsjCommand*cmd=vsj_command(f,i);Entry*e=s->entries+i;e->status=TAR_NOT_PROGRAM;
  if(!cmd){result=TAR_INVALID;break;}if(cmd->kind!=VSJ_PROGRAM_DRAW)continue;
  const VclSnapshot*proof=NULL;e->status=TAR_MISSING;
  if(topspin_constant_lineage_lookup(f,i,&proof)!=VSA_OK)continue;
  if(s->bound==TAR_MAX_BINDINGS||sizeof(VadbBinding)>TAR_MAX_BYTES-charged){result=TAR_BUDGET;break;}
  VadbBinding*b=calloc(1,sizeof*b);if(!b){result=TAR_BUDGET;break;}
  if(vadb_bind_journal(&collector,f,i,proof,b)!=VADB_OK){free(b);continue;}
  e->binding=b;e->status=TAR_OK;s->bound++;s->bytes+=sizeof*b;charged+=sizeof*b;
 }
 if(result==TAR_OK&&(!frame_ok(s,f)||!bindings_live(s)))result=TAR_INVALID;
 if(result!=TAR_OK)discard(s);else *out=s;
 busy=0;return result;
}
int topspin_actor_seal_validate(const TopspinActorSeal*s,const VsjFrame*f){
 return on&&gpu()&&!busy&&frame_ok(s,f)&&bindings_live(s);
}
const VadbBinding*topspin_actor_seal_binding(const TopspinActorSeal*s,uint32_t i,int*status){
 if(status)*status=TAR_MISSING;if(!on||!gpu()||busy||!s||i>=s->commands)return NULL;
 if(status)*status=s->entries[i].status;return s->entries[i].binding;
}
uint32_t topspin_actor_seal_count(const TopspinActorSeal*s){return on&&gpu()&&!busy&&s?s->bound:0;}
int topspin_actor_seal_release(TopspinActorSeal**out){
 if(!on)return TAR_OFF;if(!gpu()||busy)return TAR_OWNER;if(!out||!*out)return TAR_INVALID;
 busy=1;discard(*out);*out=NULL;busy=0;return TAR_OK;
}
int topspin_actor_retire_host(void){
 if(!on)return TAR_OFF;if(!gpu()||busy)return TAR_OWNER;
 busy=1;
 /* Revoked closed evidence cannot match any newly admitted source. This path
  * remains safe even when stale PB prevents a valid current-scene query. */
 uint64_t revision=tsl_authority_revision(),arena=xbox_GuestMemoryArenaEpoch();
 pthread_mutex_lock(&collector.mutex);
 for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*r=collector.records+i;
  if(r->completed==2&&(r->source.authority_revision!=revision||r->graph.memory_epoch!=arena)){r->completed=0;collector.used--;}
 }
 pthread_mutex_unlock(&collector.mutex);
 VpliPacket p;VpliSceneTag t;
 if(!vpli_packet(bridge,&p)||p.version!=VPLI_PACKET_VERSION||p.method!=0x130||!current(NULL,&t)||memcmp(&p.scene,&t,sizeof t)){busy=0;return TAR_MISSING;}
 VplSpan next=p.packet;next.end=next.begin;
 int result=vadb_retire_before(&collector,&next)==VADB_OK?TAR_OK:TAR_MISSING;
 busy=0;return result;
}
