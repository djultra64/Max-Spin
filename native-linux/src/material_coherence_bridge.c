/* No guest pointers cross the SDK93 queue. Own work djultra64. */
#include "material_coherence_bridge.h"
#include "nv2a_executor_job.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define PIXELS (512u*512u*4u)
typedef struct {McSnapshot proof;D3D8VkCoherenceDiagnostic diagnostic;unsigned char pixels[PIXELS];} McJobOutput;
static McProducerCapture producer;
static int enabled,diagnostic_enabled;
static atomic_uint diagnostic_state;
McBridgeDiagnostic topspin_material_bridge_first_failure;
int topspin_material_bridge_first_failure_ready(void){
 return atomic_load_explicit(&diagnostic_state,memory_order_acquire)==2;
}
static int rejected(McBridgeDiagnostic*d,unsigned stage,const McLock*l){
 if(diagnostic_enabled){
  d->version=1;d->stage=stage;
  if(l){d->resource=l->resource;d->parent=l->header[5];d->level=l->level;
   d->width=l->width;d->height=l->height;d->epoch=l->memory_epoch;}
  unsigned expected=0;
  if(atomic_compare_exchange_strong_explicit(&diagnostic_state,&expected,1,memory_order_acq_rel,memory_order_acquire)){
   topspin_material_bridge_first_failure=*d;
   atomic_store_explicit(&diagnostic_state,2,memory_order_release);
  }
 }
 return -1;
}
static atomic_flag busy=ATOMIC_FLAG_INIT;
static int work(const void*in,uint32_t ins,void*out,uint32_t outs){
 if(!nv2a_executor_is_owner()||!producer||ins!=sizeof(McLock)||outs!=sizeof(McJobOutput))return 0;
 McJobOutput*r=out;const McLock*l=in;
 if(!l->width||l->width>512||!l->height||l->height>512)return 0;
 /* Provider gets only owned job inputs/output. It must supply its real binding
  * ledger proof; this adapter deliberately never fabricates any identity bit. */
 if(diagnostic_enabled)d3d8_vk_coherence_diagnostic_scope(&r->diagnostic);
 int ok=producer(l,&r->proof,r->pixels,PIXELS);
 if(diagnostic_enabled)d3d8_vk_coherence_diagnostic_scope(NULL);
 r->proof.rgba=NULL; /* never publish an owner-job pointer across threads */
 return ok==1||ok==2?ok:0;
}
int mc_bridge_configure(McProducerCapture p){
 const char*s=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");
 if(!s||strcmp(s,"1"))return 1;
 if(enabled||!p)return 0;
 const char*d=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");
 diagnostic_enabled=d&&!strcmp(d,"1");
 producer=p;enabled=1;return 1;
}
int mc_bridge_snapshot(const McLock*l,McOwnedSnapshot*out){
 if(!enabled)return 0;
 McBridgeDiagnostic d={0};
 if(!l||!out)return rejected(&d,MCB_ARGUMENT,l);
 if(out->owned)return rejected(&d,MCB_ALREADY_OWNED,l);
 if(atomic_flag_test_and_set_explicit(&busy,memory_order_acquire))return rejected(&d,MCB_BUSY,l);
 McJobOutput*r=calloc(1,sizeof*r);int result=-1;unsigned stage=MCB_JOB_ALLOC;
 if(r){int callback_result=0,status=nv2a_executor_invoke_drained(work,l,sizeof*l,r,sizeof*r,2000,&callback_result);
  if(diagnostic_enabled){d.invoke_observed=1;d.invoke_status=status;d.callback_result=callback_result;}
  stage=MCB_INVOKE;
  if(status==NV2A_EXECUTOR_DONE){
   if(diagnostic_enabled)d.gpu=r->diagnostic;
   stage=MCB_CALLBACK;
   if(callback_result==1||callback_result==2){
    int same=mc_same_lock(l,&r->proof.lock);
    if(diagnostic_enabled){d.lock_compared=1;d.lock_equal=same;}
    stage=MCB_LOCK;
    if(same){
     if(callback_result==2){out->proof=r->proof;out->kind=2;out->proof.rgba=NULL;result=2;}
     else {
      /* Retain only actual pixels, preserving the existing compact copy. */
      size_t bytes=(size_t)(l->format==18?l->pitch:l->width*4u)*l->height;
      if(diagnostic_enabled){d.proof_observed=1;d.expected_bytes=bytes;d.proof_bytes=r->proof.bytes;}
      stage=MCB_BYTES;
      if(bytes&&bytes<=PIXELS&&r->proof.bytes==bytes){
       void*owned=malloc(bytes);stage=MCB_COMPACT_ALLOC;
       if(owned){memcpy(owned,r->pixels,bytes);out->proof=r->proof;
        out->owned=owned;out->proof.rgba=owned;out->kind=1;result=1;}
      }
     }
    }
   }
  }
  free(r);
 }
 atomic_flag_clear_explicit(&busy,memory_order_release);
 return result<0?rejected(&d,stage,l):result;
}
void mc_bridge_snapshot_release(McOwnedSnapshot*s){if(s){free(s->owned);memset(s,0,sizeof*s);}}
int mc_bridge_transfer(const McLock*l,McRevalidate revalidate,void*ctx){
 if(!enabled)return 0;
 if(!l||!revalidate)return -1;
 McOwnedSnapshot snapshot={0};int result=-1,kind=mc_bridge_snapshot(l,&snapshot);
 if(kind==1||kind==2){McLock current;void*destination=NULL;size_t capacity=0;memset(&current,0,sizeof current);
  if(revalidate(ctx,&current,&destination,&capacity)){
   if(kind==2&&mc_same_lock(l,&current))result=2;
   else if(kind==1&&mc_commit(l,&current,&snapshot.proof,destination,capacity))result=1;
  }
 }
 mc_bridge_snapshot_release(&snapshot);return result;
}
