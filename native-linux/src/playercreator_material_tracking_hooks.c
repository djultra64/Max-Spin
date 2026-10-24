/* Proposed Creator-only original SetRT/destruction wrappers. djultra64. */
#include "playercreator_material_tracking_hooks.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "nv2a_executor_job.h"
#include "resource_provenance.h"
#include "resource_epoch_api.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <fenv.h>
#include "material_guest_cpu.h"
#include "material_subresource.h"
#include "material_tracking_diagnostic.h"
extern void sub_00158180_gen(void);
extern void sub_00158400_gen(void);
extern void sub_00157970_gen(void);
static int enabled;static atomic_int failed;
static int diagnostic_enabled;static atomic_uint diagnostic_state;
McTrackingFailure topspin_material_first_failure;
int topspin_material_first_failure_ready(void){return atomic_load_explicit(&diagnostic_state,memory_order_acquire)==2;}
static void first_failure(McTrackingFailure*t,unsigned operation){
 if(!diagnostic_enabled)return;unsigned zero=0;
 if(!atomic_compare_exchange_strong_explicit(&diagnostic_state,&zero,1,memory_order_acq_rel,memory_order_acquire))return;
 t->operation=operation;t->end_esp=g_esp;t->version=1;
 topspin_material_first_failure=*t;atomic_store_explicit(&diagnostic_state,2,memory_order_release);
}
static int reject(McTrackingFailure*t,unsigned stage){if(diagnostic_enabled)t->stage=stage;return 0;}

static int span(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static uint32_t rd(uint32_t a){uint32_t n=0;if(span(a,4))memcpy(&n,(void*)XBOX_PTR(a),4);return n;}
static int invoke(Nv2aExecutorWork work,const void*i,uint32_t in,void*o,uint32_t out,McTrackingFailure*t){int value=0;int status=nv2a_executor_invoke(work,i,in,o,out,2000,&value);if(diagnostic_enabled&&t){t->invoke_status=status;t->invoke_value=value;}return status==NV2A_EXECUTOR_DONE&&value==1;}
static int checkpoint_work(const void*in,uint32_t ins,void*out,uint32_t outs){
 uint32_t zero=1;if(!in||ins!=4)return 0;memcpy(&zero,in,4);if(zero)return 0;return rjp_checkpoint_job(NULL,0,out,outs);
}
typedef struct {uint32_t entry,device,begin,base,size,format;uint64_t epoch;RjpCheckpoint checkpoint;int valid,scoped;uint32_t wrap_count,wrap_jump,wrap_word,wrap_reset;int wrap_valid;McTrackingFailure diagnostic;} Binding;
static _Thread_local Binding*active_binding;
static Binding before(void){Binding t={0};t.entry=g_esp;t.device=rd(0x168c68);if(!span(t.device,0x1a0c))return t;
 uint32_t candidate=rd(t.entry+4);if(!candidate)candidate=rd(t.device+0x19f4);
 if(!span(candidate,24))return t;
 uint32_t format=rd(candidate+12);t.format=(format>>8)&255u;
 if(t.format!=6u&&t.format!=18u)return t;
 if(t.format==6u&&(rd(candidate+16)||((format>>20)&15)>9||((format>>24)&15)>9))return t;
 if(t.format==18u){
  uint32_t size=rd(candidate+16),width=(size&4095u)+1u,height=((size>>12)&4095u)+1u,pitch=((size>>24)+1u)*64u;
  /* Scope only the bounded material domain, just as RGBA6 above. A real
   * multisampled backbuffer may be2560x720/pitch10240: excluding it here is
   * not CPU-only authority. Actual GPU writes remain in the native ledger. */
  if(width>512u||height>512u||pitch>2048u)return t;
 }
 /* Malformed IN-domain Size/layout still enters the scope and fails closed
  * against the exact post-original factory header; do not silently skip it. */
 t.scoped=1;if(diagnostic_enabled){t.diagnostic.entry=t.entry;t.diagnostic.device=t.device;}
 t.begin=rd(t.device);if(diagnostic_enabled)t.diagnostic.begin=t.begin;
 if(t.begin<0x80000000u||t.begin>=0x84000000u){reject(&t.diagnostic,MCT_BEFORE_CURSOR);return t;}
 if(!xbox_ContiguousEpoch(t.begin,4,&t.base,&t.size,&t.epoch)){reject(&t.diagnostic,MCT_BEFORE_EPOCH);return t;}
 if(diagnostic_enabled){t.diagnostic.base=t.base;t.diagnostic.size=t.size;t.diagnostic.epoch=t.epoch;}
 Cpu saved;cpu_save(&saved);uint32_t zero=0;int ok=invoke(checkpoint_work,&zero,sizeof zero,&t.checkpoint,sizeof t.checkpoint,&t.diagnostic);cpu_restore(&saved);
 if(ok&&t.checkpoint.version==RJP_VERSION)t.valid=1;else reject(&t.diagnostic,MCT_CHECKPOINT);
 if(diagnostic_enabled){t.diagnostic.checkpoint=t.checkpoint;t.diagnostic.checkpoint_valid=t.valid;}
 return t;
}
/* Called ONLY by hash-guarded original159CF0 after its JUMP store and
 * immediately before its original cursor-reset store. All arguments are
 * original C locals; no re-read of mutable guest PB or guessed backward PUT. */
void topspin_creator_material_wrap_observe(uint32_t device,uint32_t jump,uint32_t word,uint32_t reset){
 if(!enabled||!active_binding)return;Binding*t=active_binding;
 Cpu saved;cpu_save(&saved);t->wrap_count++;t->wrap_valid=0;
 t->wrap_jump=jump;t->wrap_word=word;t->wrap_reset=reset;
 if(t->wrap_count==1&&device==t->device&&reset==t->base&&jump>=t->begin&&
    (uint64_t)jump+4u<=(uint64_t)t->base+t->size&&!((jump|reset)&3u)&&word==((reset&0x0fffffffu)|1u)){
  uint32_t base,size;uint64_t epoch;
  if(xbox_ContiguousEpoch(jump,4,&base,&size,&epoch)&&base==t->base&&size==t->size&&epoch==t->epoch&&
     xbox_ContiguousEpoch(reset,4,&base,&size,&epoch)&&base==t->base&&size==t->size&&epoch==t->epoch)t->wrap_valid=1;
 }
 cpu_restore(&saved);
}
static int after(Binding*t,unsigned args){
 McTrackingFailure*d=&t->diagnostic;
 if(!t->scoped)return 1;
 if(!t->valid)return 0;
 if(g_esp!=t->entry+4+args)return reject(d,MCT_STACK);
 uint32_t observed_device=rd(0x168c68);if(diagnostic_enabled)d->observed_device=observed_device;
 if(observed_device!=t->device)return reject(d,MCT_DEVICE);
 uint32_t end=rd(t->device),base=0,size=0;uint64_t epoch=0;
 if(diagnostic_enabled)d->end=end;
 if(end==t->begin)return reject(d,MCT_CURSOR_ORDER);
 if(end<t->begin){
#ifdef RJP_REGISTER_WRAP_TAIL
  if(t->wrap_count!=1||!t->wrap_valid||end<=t->wrap_reset)return reject(d,MCT_WRAP_RECEIPT);
#else
  return reject(d,MCT_WRAP_RECEIPT);
#endif
 }else if(t->wrap_count)return reject(d,MCT_WRAP_RECEIPT);
 if(end<t->base||(uint64_t)end>(uint64_t)t->base+t->size||end>=0x84000000u)return reject(d,MCT_CURSOR_BOUNDS);
 if(!xbox_ContiguousEpoch(end-1,1,&base,&size,&epoch))return reject(d,MCT_END_EPOCH);
 if(diagnostic_enabled){d->after_base=base;d->after_size=size;d->after_epoch=epoch;}
 if(base!=t->base||size!=t->size||epoch!=t->epoch)return reject(d,MCT_END_IDENTITY);
 /* Reservation relocation/wrap cannot be inferred from these two pointers.
  * Reject instead of associating a possibly unrelated reservation. */
 uint32_t resource=rd(t->device+0x19f4);if(diagnostic_enabled)d->registration.resource=resource;if(!span(resource,24))return reject(d,MCT_RESOURCE);
 RjpRegister q={0};q.version=RJP_VERSION;q.resource=resource;memcpy(q.header,(void*)XBOX_PTR(resource),24);if(diagnostic_enabled)d->registration=q;
 if(((q.header[3]>>8)&255)!=t->format)return reject(d,MCT_FORMAT); /* scope must remain stable */
 if((q.header[0]&0x70000u)!=0x50000u)return reject(d,MCT_SURFACE);
 q.parent=q.header[5];
 if(q.parent){
  if(!span(q.parent,20))return reject(d,MCT_PARENT);memcpy(q.parent_header,(void*)XBOX_PTR(q.parent),20);if(diagnostic_enabled)d->registration=q;
  /* Original GetSurfaceLevel changes Data and size logarithms for each mip.
   * Admit only the exact original-factory relationship, not Data==parent.Data
   * or an address-inside-allocation heuristic. No mip contents are generated. */
  McSubresource mip;if(!mc_subresource_surface(q.parent,q.parent_header,q.header,&mip))return reject(d,MCT_MIP);
  q.width=mip.width;q.height=mip.height;q.pitch=mip.pitch;q.resolved_data=mip.bits;
 }else if(t->format==18u){
  uint32_t parent[5];memcpy(parent,q.header,sizeof parent);parent[0]=(parent[0]&~0x70000u)|0x40000u;
  McSubresource mip;if(!mc_subresource_at(parent,0,&mip))return reject(d,MCT_MIP);
  q.width=mip.width;q.height=mip.height;q.pitch=mip.pitch;q.resolved_data=mip.bits;
 }else{
  if(q.header[4])return reject(d,MCT_SIZE);
  unsigned wx=(q.header[3]>>20)&15,hy=(q.header[3]>>24)&15;if(wx>9||hy>9)return reject(d,MCT_DIMENSIONS);
  q.width=1u<<wx;q.height=1u<<hy;q.pitch=q.width*4;
  uint32_t raw=q.header[1];if(raw<0x4000000u)q.resolved_data=raw|0x80000000u;else if(raw>=0x80000000u&&raw<0x84000000u)q.resolved_data=raw;else return reject(d,MCT_DATA);
 }
 q.format=t->format;if(diagnostic_enabled)d->registration=q;
 uint32_t source_base,source_size;if(!xbox_ContiguousEpoch(q.resolved_data,q.pitch*q.height,&source_base,&source_size,&q.guest_epoch))return reject(d,MCT_SOURCE_EPOCH);
 q.ring_base=t->base;q.ring_limit=t->base+t->size;q.begin=t->begin;q.end=end;q.checkpoint=t->checkpoint.serial;
#ifdef RJP_REGISTER_WRAP_TAIL
 if(t->wrap_count)q.wrap_tail_end=t->wrap_jump+4u;
#endif
 if(diagnostic_enabled)d->registration=q;
 RjpRegistered result={0};int ok=invoke(rjp_register_job,&q,sizeof q,&result,sizeof result,d);
 if(diagnostic_enabled)d->registered=result;
 if(!ok)return reject(d,MCT_REGISTER_INVOKE);
 return result.status==1&&result.guest_id?1:reject(d,MCT_REGISTER_RESULT);
}
int topspin_creator_material_tracking_configure(void){const char*s=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");if(!s||strcmp(s,"1"))return 1;if(enabled)return 0;const char*diag=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");diagnostic_enabled=diag&&!strcmp(diag,"1");enabled=1;return 1;}
int topspin_creator_material_tracking_failed(void){return atomic_load_explicit(&failed,memory_order_acquire);}
void sub_00158180(void){Binding t={0};if(enabled)t=before();Binding*previous=active_binding;if(enabled&&t.scoped&&t.valid)active_binding=&t;sub_00158180_gen();active_binding=previous;if(enabled){Cpu saved;cpu_save(&saved);if(!after(&t,8)){first_failure(&t.diagnostic,8);atomic_store_explicit(&failed,1,memory_order_release);rjp_tracking_fault();}cpu_restore(&saved);}}
void sub_00158400(void){Binding t={0};if(enabled)t=before();Binding*previous=active_binding;if(enabled&&t.scoped&&t.valid)active_binding=&t;sub_00158400_gen();active_binding=previous;if(enabled){Cpu saved;cpu_save(&saved);if(!after(&t,12)){first_failure(&t.diagnostic,12);atomic_store_explicit(&failed,1,memory_order_release);rjp_tracking_fault();}cpu_restore(&saved);}}
void sub_00157970(void){
 uint32_t resource=enabled&&span(g_esp,8)?rd(g_esp+4):0;
 unsigned type=resource&&span(resource,24)?(rd(resource)&0x70000u):0;
 if(enabled&&resource&&(type==0x40000u||type==0x50000u)&&(((rd(resource+12)>>8)&255)==6||((rd(resource+12)>>8)&255)==18)){Cpu saved;cpu_save(&saved);RjpForget q={resource,0};int result=0;McTrackingFailure d={0};if(diagnostic_enabled){d.entry=g_esp;d.registration.resource=resource;}int ok=resource&&invoke(rjp_forget_job,&q,sizeof q,&result,sizeof result,&d);if(!ok||result!=1){reject(&d,ok?MCT_FORGET_RESULT:MCT_FORGET_INVOKE);first_failure(&d,4);atomic_store_explicit(&failed,1,memory_order_release);rjp_tracking_fault();}cpu_restore(&saved);}
 sub_00157970_gen();
}
