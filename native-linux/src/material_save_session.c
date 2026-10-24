/* Proposed Creator-only, bounded HOST snapshot session. djultra64. */
#include "material_save_session.h"
#include "material_save_transaction.h"
#include "material_save_operation.h"
#include "material_save_sources.h"
#include "material_coherence_bridge.h"
#include "material_source_lock.h"
#include "material_original_probe.h"
#include "material_subresource.h"
#include "material_save_diagnostic.h"
#include "material_tracking_diagnostic.h"
#include "playercreator_material_tracking_hooks.h"
#include "resource_epoch_api.h"
#include "resource_provenance.h"
#include "nv2a_executor_job.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <fenv.h>
#include "material_guest_cpu.h"
#define SAVE_TEXTURES (MC_SAVE_RESOURCES*10u)
/* Normal randomized appearances plus Save-generated textures exceed16MiB.
 * Retained RGBA+Morton copies remain explicitly bounded, including auxiliaries. */
#define SAVE_BYTES (32u*1024u*1024u)
typedef struct {McLock texture;McOwnedSnapshot snapshot;unsigned char*swizzled;} Entry;
typedef struct {Entry entries[SAVE_TEXTURES];McSaveSources inventory;unsigned count,depth;size_t bytes;int unexpected;uint32_t capture_caller;int synchronize;} Session;
extern int xbox_nv2a_probe_json(char*,size_t) __attribute__((weak));
static _Thread_local Session*session;
static int enabled;static atomic_uint notices;static _Thread_local int export_failed;
static int diagnostic_enabled;static atomic_uint save_diagnostic_state,copy_diagnostic_state;
static _Thread_local McSaveDiagnostic current_diagnostic;
McSaveDiagnostic topspin_material_save_first_failure;
int topspin_material_save_first_failure_ready(void){return atomic_load_explicit(&save_diagnostic_state,memory_order_acquire)==2;}
static int save_fail(unsigned stage){
 if(diagnostic_enabled){unsigned zero=0;if(atomic_compare_exchange_strong_explicit(&save_diagnostic_state,&zero,1,memory_order_acq_rel,memory_order_acquire)){
  McSaveDiagnostic d=current_diagnostic;d.version=1;d.stage=stage;topspin_material_save_first_failure=d;
  atomic_store_explicit(&save_diagnostic_state,2,memory_order_release);
 }}return 0;
}
extern int topspin_creator_material_producer_capture(const McLock*,McSnapshot*,unsigned char*,size_t);
static int span(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static int rd(void*u,uint32_t a,uint32_t*v){(void)u;if(!span(a,4))return 0;memcpy(v,(void*)XBOX_PTR(a),4);return 1;}
static uint32_t word(uint32_t a){uint32_t v=0;rd(NULL,a,&v);return v;}
static void release(Session*s){if(s){for(unsigned i=0;i<s->count;i++){mc_bridge_snapshot_release(&s->entries[i].snapshot);free(s->entries[i].swizzled);}free(s);}}
static int stable_parent(const uint32_t*a,const uint32_t*b){return((a[0]^b[0])&~0x0078ffffu)==0&&a[1]==b[1]&&a[3]==b[3]&&a[4]==b[4];}
typedef struct {uint32_t bits,bytes;uint64_t epoch;} CpuRgbSource;
static int cpu_rgb_work(const void*in,uint32_t ins,void*out,uint32_t outs){
 if(!nv2a_executor_is_owner()||!in||ins!=sizeof(CpuRgbSource)||!out||outs!=4||!rjp_enabled())return 0;
 const CpuRgbSource*q=in;uint32_t base,size,after_base,after_size;uint64_t epoch,after_epoch;XboxNv2aDrainState before,after;
 if(!xbox_Nv2aDrainSnapshot(&before)||before.get!=before.put||!xbox_ContiguousEpoch(q->bits,q->bytes,&base,&size,&epoch)||epoch!=q->epoch||rjp_cpu_classify(q->bits,q->bytes,epoch)!=0||
    !xbox_ContiguousEpoch(q->bits,q->bytes,&after_base,&after_size,&after_epoch)||after_base!=base||after_size!=size||after_epoch!=epoch||
    !xbox_Nv2aDrainSnapshot(&after)||after.get!=before.get||after.put!=before.put)return 0;
 *(uint32_t*)out=1;return 1;
}
static int cpu_rgb_texture(uint32_t texture,const uint32_t header[5]){
 /* Original7AD70/7AE10 can store X8R8G8B8 without conversion. It is not an
  * RGBA render-target snapshot. Preserve the whole original Morton mip chain
  * only with positive NEVER_GPU coverage, never on a missing GPU binding. */
 uint32_t f=header[3],wx=(f>>20)&15u,hy=(f>>24)&15u,levels=(f>>16)&15u;
 if(header[4]||(f&4u)||(f&0xf0u)!=0x20u||((f>>8)&255u)!=7u||wx>9||hy>9||!levels||levels>(wx>hy?wx:hy)+1)return 0;
 CpuRgbSource q={0};uint32_t raw=header[1],base,size;uint64_t epoch;
 q.bits=raw<0x4000000u?raw|0x80000000u:raw;
 for(unsigned i=0;i<levels;i++)q.bytes+=(1u<<(wx>i?wx-i:0))*(1u<<(hy>i?hy-i:0))*4u;
 if(!raw||q.bits<0x80000000u||(uint64_t)q.bits+q.bytes>0x84000000ull||!xbox_ContiguousEpoch(q.bits,q.bytes,&base,&size,&q.epoch))return 0;
 uint32_t proved=0;int result=0;
 if(nv2a_executor_invoke_drained(cpu_rgb_work,&q,sizeof q,&proved,sizeof proved,2000,&result)!=NV2A_EXECUTOR_DONE||result!=1||proved!=1)return 0;
 return stable_parent(header,(void*)XBOX_PTR(texture))&&xbox_ContiguousEpoch(q.bits,q.bytes,&base,&size,&epoch)&&epoch==q.epoch;
}
static int current_lock(const McLock*,McLock*);
static int capture_view(void*user,uint32_t parent,const McOriginalProbe*p){
 Session*s=user;if(diagnostic_enabled){current_diagnostic.parent=parent;current_diagnostic.surface=p->resource;current_diagnostic.level=p->level;current_diagnostic.bits=p->bits;current_diagnostic.pitch=p->pitch;current_diagnostic.count=s->count;}
 if(s->count==SAVE_TEXTURES||!span(parent,20)||!span(p->resource,24))return save_fail(MCS_CAPTURE_LIMIT);
 Entry*e=&s->entries[s->count];McLock*l=&e->texture;memset(l,0,sizeof*l);
 l->resource=p->resource;memcpy(l->header,p->surface_header,sizeof l->header);
 memcpy(l->parent_header,(void*)XBOX_PTR(parent),sizeof l->parent_header);
 McSubresource view;if(!mc_subresource_surface(parent,l->parent_header,l->header,&view)||view.level!=p->level||p->bits!=view.bits||p->pitch!=view.pitch)return save_fail(MCS_SURFACE_VIEW);
 l->data=view.data;l->bits=view.bits;l->format=(view.format>>8)&255u;l->caller=s->capture_caller;l->level=view.level;l->width=view.width;l->height=view.height;l->pitch=view.pitch;if(diagnostic_enabled){current_diagnostic.width=l->width;current_diagnostic.height=l->height;}
 /* Preflight is not an encoder call; caller0 is truthful. The retained view
  * is later matched to the actual original encoder's separately proved tuple. */
 uint32_t base,size;if(!xbox_ContiguousEpoch(l->bits,view.bytes,&base,&size,&l->memory_epoch))return save_fail(MCS_EPOCH);
 if(diagnostic_enabled)current_diagnostic.epoch=l->memory_epoch;
 if((size_t)view.bytes*2>SAVE_BYTES-s->bytes)return save_fail(MCS_PIXEL_BUDGET);
 int kind=mc_bridge_snapshot(l,&e->snapshot);if(diagnostic_enabled)current_diagnostic.snapshot_kind=kind;if(kind!=1&&kind!=2)return save_fail(MCS_GPU_SNAPSHOT);
 McLock observed;if(!current_lock(l,&observed)){mc_bridge_snapshot_release(&e->snapshot);return save_fail(MCS_FINAL_IDENTITY);}
 s->count++;if(kind==1){
  s->bytes+=(size_t)view.bytes*2;
  if(l->format==6){e->swizzled=malloc(view.bytes);if(!e->swizzled||!mc_encode_rgba_morton(e->snapshot.proof.rgba,view.width,view.height,e->swizzled,view.bytes))return save_fail(MCS_SWIZZLE);}
  if(s->synchronize){
   /* The authentic temporary Surface is still locked here. Revalidate after
    * the owner job, before the original CPU resize can consume this parent. */
   McLock current=*l;memcpy(current.header,(void*)XBOX_PTR(l->resource),sizeof current.header);
   memcpy(current.parent_header,(void*)XBOX_PTR(parent),sizeof current.parent_header);
   uint32_t b,z;uint64_t epoch;
   if(!xbox_ContiguousEpoch(l->bits,view.bytes,&b,&z,&epoch))return save_fail(MCS_EPOCH);
   current.memory_epoch=epoch;
   if(!mc_commit(l,&current,&e->snapshot.proof,(void*)XBOX_PTR(l->bits),view.bytes))return save_fail(MCS_FINAL_IDENTITY);
  }
 }
 return 1;
}
static int add_texture(Session*s,uint32_t texture){
 if(diagnostic_enabled){current_diagnostic.texture=texture;current_diagnostic.parent=0;current_diagnostic.surface=0;current_diagnostic.level=0;current_diagnostic.snapshot_kind=0;current_diagnostic.epoch=0;}
 if(!span(texture,20))return save_fail(MCS_TEXTURE);uint32_t header[5];memcpy(header,(void*)XBOX_PTR(texture),sizeof header);
 if((header[0]&0x70000u)!=0x40000u)return save_fail(MCS_TEXTURE);
 unsigned format=(header[3]>>8)&255u;if(diagnostic_enabled)current_diagnostic.format=format;
 if(format==7u)return cpu_rgb_texture(texture,header)?1:save_fail(MCS_FORMAT);
 /* BC/DXT cannot be GPU attachments. All other unhandled formats reject. */
 if(format!=6&&format!=18)return format==0x0cu||format==0x0eu||format==0x0fu?1:save_fail(MCS_FORMAT);
 McSubresource view;if(!mc_subresource_at(header,0,&view))return save_fail(MCS_BASE_MIP);
 unsigned levels=format==18?1u:(header[3]>>16)&15u,exported=0;
 /* Actual73060/730E0 truncates BC conversions at4x4; a tiny base falls back
  * to one level. Do not require producer proofs for mips the exporter won't read. */
 for(unsigned level=0;level<levels;level++){
  if(diagnostic_enabled)current_diagnostic.level=level;
  if(!mc_subresource_at(header,level,&view))return save_fail(MCS_LEVEL_MIP);
  if(level&&(view.width<4||view.height<4))break;
  McOriginalProbe probe={0};if(!mc_original_texture_level_capture(texture,level,capture_view,s,&probe))return save_fail(MCS_ORIGINAL_PROBE);
  if(!stable_parent(header,(void*)XBOX_PTR(texture)))return save_fail(MCS_PARENT_CHANGED);exported++;
 }
 return exported!=0?1:save_fail(MCS_BASE_MIP);
}
static int prepare(Session**result){
 if(topspin_creator_material_tracking_failed())return save_fail(MCS_TRACKING);
 if(mc_source_lock_failed())return save_fail(MCS_SOURCE_LOCK);
 if(export_failed)return save_fail(MCS_EXPORT);
 uint32_t model=word(0x390a54);McSaveSources list,after;
 if(diagnostic_enabled)current_diagnostic.model=model;
 if(!model)return save_fail(MCS_MODEL);
 int scanned=diagnostic_enabled?mc_save_sources_diagnostic(rd,NULL,model,&list,&current_diagnostic.inventory):mc_save_sources(rd,NULL,model,&list);
 if(!scanned)return save_fail(MCS_INVENTORY);
 if(diagnostic_enabled)current_diagnostic.inventory_count=list.count;
 Session*s=calloc(1,sizeof*s);if(!s)return save_fail(MCS_ALLOC);
 s->inventory=list;
 for(unsigned i=0;i<list.count;i++){if(diagnostic_enabled)current_diagnostic.index=i;if(!add_texture(s,list.resources[i])){release(s);return 0;}}
 /* Re-read topology after guest-turn yields; never call original callbacks a
  * second time or evaluate guessed rendering/property side effects here. */
 if(word(0x390a54)!=model||!mc_save_sources(rd,NULL,model,&after)||memcmp(&list,&after,sizeof list)){release(s);return save_fail(MCS_TOPOLOGY_CHANGED);}
 for(unsigned i=0;i<s->count;i++){
  McLock*l=&s->entries[i].texture;uint32_t parent=l->header[5],base,size;uint64_t epoch;McSubresource view;
  if(!span(parent,20)||!stable_parent(l->parent_header,(void*)XBOX_PTR(parent))||!mc_subresource_surface(parent,(void*)XBOX_PTR(parent),l->header,&view)||view.level!=l->level||!xbox_ContiguousEpoch(l->bits,view.bytes,&base,&size,&epoch)||epoch!=l->memory_epoch){release(s);return save_fail(MCS_FINAL_IDENTITY);}
 }
 s->depth=1;*result=s;return 1;
}
int mc_save_session_configure(void){const char*s=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");if(!s||strcmp(s,"1"))return 1;if(enabled)return 0;const char*diag=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");diagnostic_enabled=diag&&!strcmp(diag,"1");enabled=1;return 1;}
void mc_save_session_operation_begin(void){
 if(!enabled)return;
 if(session||!mc_save_operation_active()){mc_save_operation_reject();return;}
 /* Recoverable errors belong to the preceding operation; RJP loss of tracking
  * remains latched in its independent ledger and cannot be cleared here. */
 mc_source_lock_operation_begin();export_failed=0;
}
int mc_save_before(void){
 if(!enabled)return 1;
 if(session){session->depth++;return 1;} /* original path-normalization recursion */
 Cpu saved;cpu_save(&saved);Session*s=NULL;if(diagnostic_enabled)memset(&current_diagnostic,0,sizeof current_diagnostic);
 int ok=mc_save_operation_active()&&span(g_esp,8)?prepare(&s):save_fail(MCS_STACK);
 if(ok){ok=mc_save_operation_phase_begin();if(!ok)save_fail(MCS_TRANSACTION);}
 if(!ok){release(s);mc_save_operation_reject();}
 cpu_restore(&saved);
 if(ok){session=s;return 1;}
 unsigned n=atomic_fetch_add_explicit(&notices,1,memory_order_relaxed);if(n<8){
  fprintf(stderr,"[CREATOR-MATERIAL] Save rejected before original C66A0: coherent source set incomplete; no original Save writer invoked\n");
  /* Decision is already final. Only acquire-published HOST-owned facts are
   * printed here; no VM reads, owner calls or new source authority. */
  if(diagnostic_enabled&&topspin_material_first_failure_ready()){
   const McTrackingFailure*t=&topspin_material_first_failure;
   fprintf(stderr,"[CREATOR-MATERIAL-FIRST-TRACKING] stage=%u operation=%u begin=%08x end=%08x epoch=%llu invoke_status=%d invoke_value=%d\n",t->stage,t->operation,t->begin,t->end,(unsigned long long)t->epoch,t->invoke_status,t->invoke_value);
  }
  if(diagnostic_enabled&&topspin_material_bridge_first_failure_ready()){
   const McBridgeDiagnostic*b=&topspin_material_bridge_first_failure;
   if(b->gpu.stage){const D3D8VkCoherenceDiagnostic*g=&b->gpu;const RjpQuery*q=&g->query;
    fprintf(stderr,"[CREATOR-MATERIAL-FIRST-GPU] stage=%u observed=%u classify=%d backend_result=%d bytes=%u resource=%08x parent=%08x data=%08x mip=%u width=%u height=%u pitch=%u epoch=%llu allocation=%llu contents=%llu base=%08x size=%u actual_epoch=%llu before_get=%08x before_put=%08x after_base=%08x after_size=%u after_epoch=%llu\n",g->stage,g->observed,g->classify,g->backend_result,g->bytes,q->resource,q->parent,q->resolved_data,q->level,q->width,q->height,q->pitch,(unsigned long long)q->guest_epoch,(unsigned long long)q->allocation,(unsigned long long)q->contents,g->base,g->size,(unsigned long long)g->epoch,g->get,g->put,g->after_base,g->after_size,(unsigned long long)g->after_epoch);
    if(g->rjp.version){const RjpQueryDiagnostic*d=&g->rjp;const RjpRegister*o=&d->object;
     fprintf(stderr,"[CREATOR-RJP] broken=%u fault=%u pending=%llu retired_resource=%u retired_parent=%u live_objects=%u related=%u same=%u writes_used=%u allocation_writes=%u color_writes=%u\n",d->broken,d->tracking_fault,(unsigned long long)d->pending_checkpoint,d->retired_resource,d->retired_parent,d->live_objects,d->related_objects,d->same_objects,d->writes_used,d->allocation_writes,d->color_writes);
     fprintf(stderr,"[CREATOR-RJP-OBJECT] id=%llu live=%u surface_live=%u same=%u resource=%08x parent=%08x data=%08x width=%u height=%u pitch=%u format=%u epoch=%llu checkpoint=%llu begin=%08x end=%08x\n",(unsigned long long)d->object_id,d->object_live,d->surface_live,d->object_same,o->resource,o->parent,o->resolved_data,o->width,o->height,o->pitch,o->format,(unsigned long long)o->guest_epoch,(unsigned long long)o->checkpoint,o->begin,o->end);
     fprintf(stderr,"[CREATOR-RJP-WRITE] serial=%llu target=%llu binding=%llu allocation=%llu contents=%llu epoch=%llu color=%08x width=%u height=%u pitch=%u format=%u\n",(unsigned long long)d->write_serial,(unsigned long long)d->write_target,(unsigned long long)d->write_binding,(unsigned long long)d->write_allocation,(unsigned long long)d->write_contents,(unsigned long long)d->write_epoch,d->write_color,d->write_width,d->write_height,d->write_pitch,d->write_format);
     fprintf(stderr,"[CREATOR-RJP-HEADERS] surface=%08x,%08x,%08x,%08x,%08x,%08x parent=%08x,%08x,%08x,%08x,%08x query=%08x,%08x,%08x,%08x,%08x,%08x\n",o->header[0],o->header[1],o->header[2],o->header[3],o->header[4],o->header[5],o->parent_header[0],o->parent_header[1],o->parent_header[2],o->parent_header[3],o->parent_header[4],q->header[0],q->header[1],q->header[2],q->header[3],q->header[4],q->header[5]);
    }
   }
   if(b->invoke_status==-1&&xbox_nv2a_probe_json){char state[1024];if(xbox_nv2a_probe_json(state,sizeof state)>0)fprintf(stderr,"[CREATOR-MATERIAL-DRAIN-TIMEOUT] %s\n",state);}
   fprintf(stderr,"[CREATOR-MATERIAL-FIRST-BRIDGE] stage=%u resource=%08x parent=%08x mip=%u width=%u height=%u epoch=%llu invoke_observed=%u status=%d callback=%d lock_compared=%u lock_equal=%d proof_observed=%u expected_bytes=%llu proof_bytes=%llu\n",b->stage,b->resource,b->parent,b->level,b->width,b->height,(unsigned long long)b->epoch,b->invoke_observed,b->invoke_status,b->callback_result,b->lock_compared,b->lock_equal,b->proof_observed,(unsigned long long)b->expected_bytes,(unsigned long long)b->proof_bytes);
  }
  if(diagnostic_enabled&&topspin_material_save_first_failure_ready()){
   const McSaveDiagnostic*d=&topspin_material_save_first_failure;
   fprintf(stderr,"[CREATOR-MATERIAL-FIRST-PREFLIGHT] stage=%u model=%08x index=%u texture=%08x parent=%08x surface=%08x mip=%u kind=%d format=%u width=%u height=%u epoch=%llu\n",d->stage,d->model,d->index,d->texture,d->parent,d->surface,d->level,d->snapshot_kind,d->format,d->width,d->height,(unsigned long long)d->epoch);
   if(d->inventory.stage){const McSaveSourcesDiagnostic*i=&d->inventory;fprintf(stderr,"[CREATOR-MATERIAL-FIRST-INVENTORY] stage=%u address=%08x value=%08x node=%08x vtable=%08x save=%08x property=%08x descriptor=%08x count=%u depth=%u nodes=%u properties=%u\n",i->stage,i->address,i->value,i->node,i->vtable,i->save_callback,i->property,i->descriptor,i->count,i->depth,i->nodes,i->properties);}
  }
 }
 SET_LO8(g_eax,0);g_esp+=8;return 0;
}
void mc_save_after(void){
 if(!enabled||!session)return;if(--session->depth)return;
 Session*s=session;session=NULL;
 Cpu saved;cpu_save(&saved);
 int complete=LO8(g_eax)!=0&&!s->unexpected&&!mc_source_lock_failed()&&!export_failed;
 mc_save_operation_phase_end(complete);
 int committed=complete; /* Publication belongs to the original outer Save seam. */
 if(diagnostic_enabled&&!committed)fprintf(stderr,"[CREATOR-MATERIAL-SAVE-END] original_al=%u complete=%d unexpected=%d source_failed=%d export_failed=%d transaction_failed=%d entries=%u bytes=%zu\n",LO8(g_eax),complete,s->unexpected,mc_source_lock_failed(),export_failed,mc_save_transaction_failed(),s->count,s->bytes);
 if(diagnostic_enabled&&!committed&&topspin_material_save_first_failure_ready()){
  const McSaveDiagnostic*d=&topspin_material_save_first_failure;
  fprintf(stderr,"[CREATOR-MATERIAL-FIRST-SOURCE] stage=%u texture=%08x mip=%u format=%u width=%u height=%u\n",d->stage,d->texture,d->level,d->format,d->width,d->height);
 }
 /* A rejected Save source is not evidence of lost GPU tracking. */
 release(s);cpu_restore(&saved);
 if(!committed)SET_LO8(g_eax,0);
}
static int current_lock(const McLock*l,McLock*c){
 uint32_t parent=l->header[5],base,size;uint64_t epoch;McSubresource view;
 if(!span(l->resource,24)||!span(parent,20))return 0;
 *c=*l;memcpy(c->header,(void*)XBOX_PTR(l->resource),sizeof c->header);
 memcpy(c->parent_header,(void*)XBOX_PTR(parent),sizeof c->parent_header);
 if(!stable_parent(c->parent_header,l->parent_header)||
    ((c->header[0]^l->header[0])&~0x0078ffffu)||
    !mc_subresource_surface(parent,c->parent_header,c->header,&view)||
    view.data!=l->data||view.bits!=l->bits||view.pitch!=l->pitch||view.level!=l->level||
    view.width!=l->width||view.height!=l->height||((view.format>>8)&255u)!=l->format||
    !xbox_ContiguousEpoch(l->bits,view.bytes,&base,&size,&epoch))return 0;
 /* Original factory reference counts may change without a storage rebind. */
 c->header[0]=l->header[0];c->parent_header[0]=l->parent_header[0];c->memory_epoch=epoch;
 return mc_same_lock(l,c);
}
int mc_save_source_copy(const McLock*l,void*destination,size_t capacity){
 if(!session)return 0;
 if(!l){session->unexpected=1;return -1;}
 uint32_t parent=l->header[5];
 /* Original Save legitimately creates/reuses auxiliary/conversion textures
  * after preflight (live153 recycled a linear ScoreName header for RGBA256).
  * Preflight topology is an early guard, not the authority for later bytes.
  * Authenticate this actual consumer and capture its CURRENT producer once;
  * the post-job full identity check still rejects a concurrent rebind. */
 if(l&&destination&&((l->format==6&&mc_encoder_source_caller(l->caller))||
                      (l->format==18&&mc_linear_source_caller(l->caller)))){
  McOwnedSnapshot fresh={0};int kind=mc_bridge_snapshot(l,&fresh),result=0;
  McLock current;int valid=current_lock(l,&current);
  if(valid&&kind==2&&mc_same_lock(l,&current))result=2; /* explicit NEVER_GPU */
  else if(valid&&kind==1)result=mc_commit(l,&current,&fresh.proof,destination,capacity);
  mc_bridge_snapshot_release(&fresh);if(result)return result;
 }
 if(diagnostic_enabled&&!atomic_exchange_explicit(&copy_diagnostic_state,1,memory_order_acq_rel)){fprintf(stderr,"[CREATOR-MATERIAL-COPY-MISS] parent=%08x mip=%u bits=%08x data=%08x width=%u height=%u pitch=%u epoch=%llu entries=%u\n",parent,l->level,l->bits,l->data,l->width,l->height,l->pitch,(unsigned long long)l->memory_epoch,session->count);
  for(unsigned i=0;i<session->count;i++){Entry*e=&session->entries[i];if(e->texture.header[5]!=parent||e->texture.level!=l->level)continue;McLock*t=&e->texture;McSnapshot*p=&e->snapshot.proof;fprintf(stderr,"[CREATOR-MATERIAL-COPY-ENTRY] data=%08x bits=%08x epoch=%llu width=%u height=%u pitch=%u kind=%d swizzled=%d parent_equal=%d bytes=%zu layout=%u flags=%u allocation=%llu observed_allocation=%llu contents=%llu observed_contents=%llu generation=%llu observed_generation=%llu producer=%llu completed=%llu capacity=%zu\n",t->data,t->bits,(unsigned long long)t->memory_epoch,t->width,t->height,t->pitch,e->snapshot.kind,e->swizzled!=NULL,stable_parent(l->parent_header,t->parent_header),p->bytes,p->layout,p->flags,(unsigned long long)p->allocation,(unsigned long long)p->observed_allocation,(unsigned long long)p->contents,(unsigned long long)p->observed_contents,(unsigned long long)p->guest_generation,(unsigned long long)p->observed_guest_generation,(unsigned long long)p->producer_serial,(unsigned long long)p->completed_serial,capacity);break;}
 }
 session->unexpected=1;return -1;
}

int mc_save_active(void){return enabled&&session!=NULL;}
static int validate_linear_view(void*user,uint32_t parent,const McOriginalProbe*p){
 Session*s=user;McLock l={0};McSubresource view;uint32_t base,size;
 if(!span(parent,20)||!span(p->resource,24))return 0;
 l.resource=p->resource;memcpy(l.header,p->surface_header,sizeof l.header);
 memcpy(l.parent_header,(void*)XBOX_PTR(parent),sizeof l.parent_header);
 if(!mc_subresource_surface(parent,l.parent_header,l.header,&view)||p->level||view.level||p->bits!=view.bits||p->pitch!=view.pitch)return 0;
 l.data=view.data;l.bits=view.bits;l.format=(view.format>>8)&255u;l.width=view.width;l.height=view.height;l.pitch=view.pitch;l.caller=s->capture_caller;
 if(l.format!=18||!xbox_ContiguousEpoch(l.bits,view.bytes,&base,&size,&l.memory_epoch))return 0;
 return mc_save_source_copy(&l,(void*)XBOX_PTR(l.bits),view.bytes)>0;
}
/* First auxiliary rejection: HOST-owned values only; no extra guest query. */
typedef struct {uint32_t stage,resource,caller,format,known;uint64_t registered_epoch;} AuxFailure;
static _Thread_local AuxFailure auxiliary_context;
static atomic_uint auxiliary_notices;
static int aux_reject(unsigned stage){if(diagnostic_enabled)auxiliary_context.stage=stage;return 0;}
static int auxiliary(Session*s,uint32_t resource,uint32_t caller,int linear){
 if(diagnostic_enabled){memset(&auxiliary_context,0,sizeof auxiliary_context);auxiliary_context.resource=resource;auxiliary_context.caller=caller;
  for(unsigned i=0;i<s->inventory.count;i++)auxiliary_context.known|=s->inventory.resources[i]==resource;
  for(unsigned i=0;i<s->count;i++)if(s->entries[i].texture.header[5]==resource){auxiliary_context.registered_epoch=s->entries[i].texture.memory_epoch;break;}
 }
 if(!span(resource,20))return aux_reject(1);
 unsigned format=(word(resource+12)>>8)&255u;if(diagnostic_enabled)auxiliary_context.format=format;
 if((linear&&(format!=18||!mc_linear_source_caller(caller)))||(!linear&&(format!=6||(caller!=0xec85du&&caller!=0xec8d8u))))return aux_reject(2);
 /* Each authentic text seam proves its CURRENT producer and complete identity,
  * including repeated factory returns before CPU resize. Never append a stale
  * duplicate or reuse retained pixel bytes as authority for a new generation. */
 for(unsigned i=0;i<s->inventory.count;i++)if(s->inventory.resources[i]==resource){
  if(!linear)return aux_reject(3);
  s->capture_caller=caller;McOriginalProbe p={0};
  int ok=mc_original_texture_level_capture(resource,0,validate_linear_view,s,&p);
  s->capture_caller=0;return ok?1:aux_reject(4);
 }
 if(s->inventory.count>=MC_SAVE_RESOURCES)return aux_reject(1);
 s->capture_caller=caller;s->synchronize=linear;
 int ok=add_texture(s,resource);s->capture_caller=0;s->synchronize=0;
 if(!ok)return aux_reject(5);
 s->inventory.resources[s->inventory.count++]=resource;return 1;
}
int mc_save_aux_source(uint32_t resource,uint32_t caller){
 if(!mc_save_active())return 0;
 if(session->unexpected)return -1;
 if(!auxiliary(session,resource,caller,1)){
  session->unexpected=1;
  if(diagnostic_enabled&&atomic_fetch_add_explicit(&auxiliary_notices,1,memory_order_relaxed)<8){AuxFailure d=auxiliary_context;
   fprintf(stderr,"[CREATOR-MATERIAL-AUX-MISS] stage=%u resource=%08x caller=%08x format=%u known=%u registered_epoch=%llu capture_stage=%u entries=%u bytes=%llu\n",d.stage,d.resource,d.caller,d.format,d.known,(unsigned long long)d.registered_epoch,topspin_material_save_first_failure_ready()?topspin_material_save_first_failure.stage:0,session->count,(unsigned long long)session->bytes);
  }return -1;
 }
 return 1;
}
/* Complete direct original export families, not a growing list of captured
 * pointers. Material routines resolve D6E20 again and may replace property
 * textures during Save, so membership is collected from the LIVE typed graph.
 * Preview/text are independent original producers with separate exact seams. */
static int material_export_caller(uint32_t c){return c==0xd79d8u||c==0xd7bf7u;}
static int vignette_export_caller(uint32_t c){return c==0xec85du||c==0xec8d8u;}
static int text_export_caller(uint32_t c){return c==0x734ecu||c==0x7371cu;}
static int current_material_source(Session*s,uint32_t resource){
 uint32_t model=word(0x390a54);McSaveSources current;
 if(!model||!mc_save_sources(rd,NULL,model,&current))return 0;
 int member=0;for(unsigned i=0;i<current.count;i++)member|=current.resources[i]==resource;
 if(!member)return 0;
 for(unsigned i=0;i<s->inventory.count;i++)if(s->inventory.resources[i]==resource)return 1;
 if(s->inventory.count==MC_SAVE_RESOURCES||!add_texture(s,resource))return 0;
 s->inventory.resources[s->inventory.count++]=resource;return 1;
}
void mc_save_export_validate(uint32_t resource,uint32_t target_format){
 if(!enabled)return;
 unsigned format=span(resource,20)?(word(resource+12)>>8)&255u:0;
 int supported=span(resource,20)&&(format!=6u||target_format==0x0cu||target_format==0x0eu||target_format==0x0fu);
 if(!supported){export_failed=1;if(session)session->unexpected=1;return;}
 if(!session)return;
 uint32_t caller=word(g_esp);int ok=0;
 int texture=(word(resource)&0x70000u)==0x40000u;
 if(texture&&material_export_caller(caller)&&(format==6u||format==7u||format==0x0cu||format==0x0eu||format==0x0fu)){
  ok=current_material_source(session,resource);
  /* Preflight coverage cannot authorize a later writer. Recheck at the real
   * exporter seam, after it resolves the current original material source. */
  if(ok&&format==7u){uint32_t header[5];memcpy(header,(void*)XBOX_PTR(resource),sizeof header);ok=cpu_rgb_texture(resource,header);}
 }
 else if(texture&&vignette_export_caller(caller)&&format==6u&&target_format==0x0eu){
  int known=0;for(unsigned i=0;i<session->inventory.count;i++)known|=session->inventory.resources[i]==resource;
  ok=known||auxiliary(session,resource,caller,0);
 }else if(texture&&text_export_caller(caller)&&format==18u)
  ok=mc_save_aux_source(resource,caller)==1;
 if(!ok){if(diagnostic_enabled&&!session->unexpected)fprintf(stderr,"[CREATOR-MATERIAL-EXPORT-MISS] resource=%08x format=%u target_format=%u caller=%08x\n",resource,format,target_format,caller);session->unexpected=1;}
}
