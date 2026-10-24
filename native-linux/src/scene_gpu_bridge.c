/* Authority follows actual consumed bytes, never GPU-job timing. djultra64. */
#include "scene_gpu_bridge.h"
#include "scene_actor_frames.h"
#include "game_scene_lifecycle_hooks.h"
#include "visual_scene_provider.h"
#include "visual_pb_hooks.h"
#include "visual_pb_lineage_runtime.h"
#include "scene_render_runtime.h"
#include "nv2a_executor_job.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
extern uint64_t SDL_GetTicksNS(void);
extern ptrdiff_t xbox_GetMemoryOffset(void);
static int enabled;
static VpliBridge bridge;
static VplRuntime memory;
static VpliSceneTag bound; /* actual decoder owner only */
static int flag(const char*n){const char*v=getenv(n);return v&&!strcmp(v,"1");}
static int owner(void*u){(void)u;return nv2a_executor_is_owner();}
static int observer(void*u){(void)u;return owner(NULL)||topspin_pb_observe_scope_active();}
static uint64_t authority(void*u){(void)u;return tsl_authority_revision();}
static int valid(const VpliSceneTag*s){return s&&s->version==VPLI_SCENE_TAG_VERSION&&!s->reserved&&s->scene&&s->view&&s->authority_revision&&s->boundary_ns;}
static int current(VpliSceneTag*out){
 if(!enabled||!out||!owner(NULL)||topspin_scene_lifecycle_failed()||!vpli_consumed_scene(&bridge,out)||!valid(out))return 0;
 return out->authority_revision==tsl_authority_revision();
}
static int admission(void*u){
 (void)u;VpliSceneTag s;
 return current(&s)&&bound.scene==s.scene&&bound.view==s.view&&bound.authority_revision==s.authority_revision;
}
static void bind_scene(void*u,const VpliSceneTag*tag){
 (void)u;VpliSceneTag consumed;
 if(!owner(NULL))return;
 /* Current getter also rejects a byte fault AFTER the previous successful bind.
  * A newer staged CPU tag has no effect on this consumed interval. */
 if(!current(&consumed)||memcmp(&consumed,tag,sizeof consumed)){
  memset(&bound,0,sizeof bound);(void)d3d8_vk_scene_continuity((VfpGeneration){0,0});return;
 }
 bound=consumed;
 if(!d3d8_vk_scene_continuity((VfpGeneration){consumed.scene,consumed.view}))memset(&bound,0,sizeof bound);
}
static int control(void*u,uint32_t va,uint32_t*out){
 (void)u;
 if(!out||!observer(NULL)||(va&3)||!((va>=0x10000u&&va<=0x4000000u-4)||va==0xfd800040u))return 0;
 memcpy(out,(const void*)((uintptr_t)xbox_GetMemoryOffset()+va),4);return 1;
}
static void fault(void*u,unsigned reason){(void)u;topspin_scene_render_lineage_fault(reason);}
static int publish(void*u,const TslSnapshot*s){
 (void)u;if(!enabled||!s)return 0;
 VpliSceneTag tag={0};
 if(s->version==TSL_VERSION)tag=(VpliSceneTag){VPLI_SCENE_TAG_VERSION,s->simulation_tick,s->simulation_time_bits,0,s->scene_id,s->view_id,s->authority_revision,SDL_GetTicksNS()};
 /* Full CPU guard and trusted bounded-reader scope are inside this helper.
  * No executor invoke, guest-turn release, queue draining or GPU operation. */
 return topspin_pb_lineage_stage_scene(&tag);
}
int topspin_scene_gpu_init(void){
 int want=flag("TOPSPIN_VISUAL_SCENE_SNAPSHOT");
 int actors=flag("TOPSPIN_VISUAL_ACTOR_BINDING");if(actors&&!want)return 0;
 if(want){
  if(enabled||!flag("RECOMP_EXECUTOR_INVOKE")||!flag("TOPSPIN_VISUAL_SCENE_LIFECYCLE")||!flag("TOPSPIN_VISUAL_PB_LINEAGE")||!flag("TOPSPIN_VISUAL_SCENE_RECORD"))return 0;
  memory=(VplRuntime){NULL,observer};
  VpliConfig c={.memory=vpl_runtime_reader(&memory,1),.decoder_owner=owner,.control_read=control,.fault=fault,.bind_scene=bind_scene,.authority_revision=authority};
  if(!vpli_configure(&bridge,&c,1)||!d3d8_vk_scene_configure(admission,NULL)||!topspin_scene_render_init()||!vpli_hooks_install(&bridge,topspin_scene_render_lineage,NULL))return 0;
  enabled=1;
 }
 if(!topspin_scene_lifecycle_configure(want?publish:NULL,NULL))return 0;
 /* Actor hooks require the real configured PB bridge AND constructor registry. */
 return topspin_scene_actor_init(actors);
}
int topspin_scene_gpu_current(TopspinSceneHostStamp*out){
 VpliSceneTag s;
 if(!out||!admission(NULL)||!current(&s))return 0;
 *out=(TopspinSceneHostStamp){VPLI_SCENE_TAG_VERSION,s.simulation_tick,s.simulation_time_bits,0,s.scene,s.view,s.authority_revision,s.boundary_ns};return 1;
}
