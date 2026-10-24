/* Real consumed-PB source -> recorder -> owned discrete frames. djultra64.
 * This installs capture only. No extra rendering, Present or guest calls. */
#include "scene_render_runtime.h"
#include "scene_actor_frames.h"
#include "constant_lineage_runtime.h"
#include "scene_gpu_bridge.h"
#include "scene_output_frames.h"
#include "visual_scene_recorder.h"
#include "visual_scene_gpu_draw.h"
#include "nv2a_executor_job.h"
#include <stdatomic.h>
#include <string.h>
extern uint64_t SDL_GetTicksNS(void);
static VsrRecorder*recorder;
static VsoOutput*output;
static atomic_uint_fast64_t faults;
/* Executor-only observations of fault sequence. No mutex recursion in fault. */
static uint64_t seen_fault,bad_frame;
static int bad_frame_valid;
static int owner(void*u){(void)u;return nv2a_executor_is_owner();}
void topspin_scene_render_lineage_fault(unsigned reason){if(reason)atomic_fetch_add_explicit(&faults,1,memory_order_release);}
static int source_tag(void*u,VsrSourceTag*out){
 (void)u;TopspinSceneHostStamp s;
 if(!out||!owner(NULL)||!topspin_scene_gpu_current(&s)||s.version!=1||s.reserved)return 0;
 *out=(VsrSourceTag){1,s.simulation_tick,s.time_bits,0,s.scene,s.view,s.authority_revision,s.boundary_ns};return 1;
}
static int current(void*u,uint64_t frame,VsjStamp*out){
 (void)u;if(!out||!owner(NULL))return 0;
 uint64_t f=atomic_load_explicit(&faults,memory_order_acquire);
 if(f!=seen_fault){seen_fault=f;bad_frame=frame;bad_frame_valid=1;}
 if(bad_frame_valid&&frame==bad_frame)return 0;
 VsrSourceTag s;if(!source_tag(NULL,&s))return 0;
 *out=(VsjStamp){frame,s.simulation_tick,0,{s.scene,s.view}};return 1;
}
static int provider(void*u,VfpProvider*p,uint64_t*e){(void)u;return d3d8_vk_scene_provider(p,e);}
static int prepare(void*u){(void)u;return d3d8_vk_scene_prepare();}
static int bindings(void*u,unsigned f,VfpResource*r,unsigned n,unsigned*c){(void)u;return d3d8_vk_scene_bindings(f,r,n,c);}
static int upload(void*u,uint64_t i,VfpResource*r){(void)u;return d3d8_vk_scene_upload_resource(i,r);}
static int material(void*u,void*p,uint32_t n,uint32_t*b){(void)u;return d3d8_vk_scene_resolved_draw(p,n,b);}
static int view(void*u,D3D8SceneViewState*v){(void)u;return d3d8_vk_scene_view_state(v);}
static int completed(void*u,uint32_t c,VfpResource*r){(void)u;return d3d8_vk_scene_completed_resource(c,r);}
static int host_policy(void*u,D3D8SceneHostPolicy*p){(void)u;return d3d8_vk_scene_host_policy(p);}
static void invalidate(void*u,uint32_t reason){
 (void)u;(void)reason;
 /* Recorder calls on actual owner only. A producer-side lineage fault uses
  * the separate atomic function above and never releases Vulkan resources. */
 if(owner(NULL)&&output)(void)vso_invalidate(output);
}
static int publish(void*u,VsjFrame**frame,const VsrHostPolicy*p){
 (void)u;
 if(!owner(NULL)||!frame||!*frame||!p||p->version!=1)return 0;
 const VsjStamp*s=vsj_stamp(*frame);VsjStamp c;VsrSourceTag tag;
 if(!s||!current(NULL,s->frame_serial,&c)||!source_tag(NULL,&tag)||memcmp(&tag,&p->source,sizeof tag)||s->simulation_tick!=c.simulation_tick||s->continuity.scene!=c.continuity.scene||s->continuity.view!=c.continuity.view)return 0;
 if(!output){VsoConfig cfg={.owner=owner,.rate=60};output=vso_create(&cfg);if(!output)return 0;if(!topspin_scene_actor_output(output)){(void)vso_destroy(&output);return 0;}}
 /* Visual scheduling candidate only: original counter is sampled, never
  * changed. Tick wrap rejects its nonmonotonic pair in the clock. No output
  * service is installed and no >60 mode is admitted by this discrete glue. */
 uint64_t sample=((uint64_t)tag.simulation_tick*1000000000u)/60u;
 int accepted=topspin_scene_actor_adopt(output,frame,sample,SDL_GetTicksNS(),p,sizeof*p)==1;
 /* Actor helper copied any owned proofs before retirement. Future camera/ball
  * finalization must also copy before here; never consult the latest ledger. */
 topspin_constant_lineage_retire(s->frame_serial);
 return accepted;
}
int topspin_scene_render_init(void){
 if(recorder||!topspin_constant_lineage_init())return 0;
 VsrConfig c={.owner=owner,.current=current,.source_tag=source_tag,.provider=provider,.prepare=prepare,.bindings=bindings,.upload=upload,.material=material,.view=view,.completed=completed,.host_policy=host_policy,.publish=publish,.invalidate=invalidate,.max_bytes=VSJ_MAX_BYTES,.upload_budget=VSR_MAX_UPLOAD};
 recorder=vsr_create(&c);return recorder!=NULL;
}
int topspin_scene_render_lineage(void*u,const VpliPacket*p,const VfpSource*s){
 (void)u;VsrSourceTag tag;
 if(!owner(NULL)||!recorder||!p||p->version!=VPLI_PACKET_VERSION||!source_tag(NULL,&tag))return 0;
 VsrSourceTag source={p->scene.version,p->scene.simulation_tick,p->scene.simulation_time_bits,p->scene.reserved,p->scene.scene,p->scene.view,p->scene.authority_revision,p->scene.boundary_ns};
 if(memcmp(&tag,&source,sizeof tag))return 0;
 VsrPacketLineage packet={1,p->header_word,p->subchannel,p->method,p->packet.identity,p->packet.epoch,p->packet.wrap_generation,p->packet.begin,p->packet.end,source};
 VfpProvider actual_provider;uint64_t epoch=0;
 if(d3d8_vk_scene_provider(&actual_provider,&epoch))
  (void)topspin_constant_lineage_capture(p,s,epoch);
 /* Missing constant ownership disables interpolation, not original draws. */
 return vsr_program_lineage_status(recorder,&packet,s);
}
