/* No guest rewrite until the exact version/identity transaction succeeds.
 * Original15E7D0 itself remains authoritative and executes once. djultra64. */
#include "material_source_lock.h"
#include "material_save_session.h"
#include "material_subresource.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "resource_epoch_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fenv.h>
#include <stdatomic.h>
#include <stdio.h>
#include "material_guest_cpu.h"
static int enabled,diagnostic_enabled;
/* Failures belong to the guest thread/operation that observed the Lock.
 * Owner callbacks never write this latch; another operation cannot reset it. */
static _Thread_local int failed;static atomic_uint observed_locks;
static void source_failed(unsigned stage,const McSourceLock*t){
 int previous=failed;failed=1;
 if(!previous&&diagnostic_enabled){const McLock*l=&t->lock;fprintf(stderr,"[CREATOR-MATERIAL-LOCK-FAIL] stage=%u entry=%08x rect=%08x resource=%08x parent=%08x data=%08x bits=%08x format=%u width=%u height=%u pitch=%u mip=%u caller=%08x header_format=%08x\n",stage,t->entry,t->locked_rect,l->resource,l->header[5],l->data,l->bits,l->format,l->width,l->height,l->pitch,l->level,l->caller,l->header[3]);}
}
static int span(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static uint32_t rd(uint32_t a){uint32_t v=0;if(span(a,4))memcpy(&v,(void*)XBOX_PTR(a),4);return v;}
static int identity(uint32_t resource,McLock*l){
 if(!span(resource,24))return 0;
 l->resource=resource;memcpy(l->header,(void*)XBOX_PTR(resource),24);l->data=l->header[1];
 if((l->header[0]&0x70000)!=0x50000||((l->header[3]>>8)&255)!=6||l->header[4])return 0;
 uint32_t parent=l->header[5];if(!span(parent,20))return 0;
 memcpy(l->parent_header,(void*)XBOX_PTR(parent),20);McSubresource view;
 if(!mc_subresource_surface(parent,l->parent_header,l->header,&view))return 0;
 l->level=view.level;return 1;
}
int mc_source_lock_configure(McProducerCapture p){const char*s=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");if(!s||strcmp(s,"1"))return 1;if(enabled||!mc_bridge_configure(p))return 0;const char*d=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");diagnostic_enabled=d&&!strcmp(d,"1");enabled=1;return 1;}
int mc_source_lock_failed(void){return failed;}
void mc_source_lock_operation_begin(void){if(enabled)failed=0;}
void mc_source_lock_begin(McSourceLock*t){
 memset(t,0,sizeof*t);if(!enabled)return;
 uint32_t e=g_esp;
 if(!span(e,0x20)||rd(e)!=0x16232d)return;
 uint32_t caller=rd(e+0x1c);if(!mc_encoder_source_caller(caller))return;
 uint32_t descriptor_offset=caller==0x7b3ffu?0xd4u:0xc0u;
 /* Once inside the exact original encoder chain, unsupported/invalid source
  * tuples are failures, not permission to save stale bytes. End rolls back. */
 if(!span(e,descriptor_offset+28)||rd(e+8)||rd(e+12)||rd(e+20)||rd(e+24)){source_failed(1,t);return;}
 t->entry=e;t->locked_rect=rd(e+16);if(!span(t->locked_rect,8)){source_failed(2,t);return;}
 if(!identity(rd(e+4),&t->lock)){source_failed(3,t);return;}
 t->descriptor_address=e+descriptor_offset;memcpy(t->descriptor,(void*)XBOX_PTR(t->descriptor_address),28);
 t->lock.format=t->descriptor[0];t->lock.width=t->descriptor[5];t->lock.height=t->descriptor[6];t->lock.caller=caller;
 unsigned w=(t->lock.header[3]>>20)&15,h=(t->lock.header[3]>>24)&15;
 if(w>9||h>9||t->lock.width!=(1u<<w)||t->lock.height!=(1u<<h)||t->lock.format!=6){source_failed(4,t);return;}
 t->candidate=1;
}
static int revalidate(void*ctx,McLock*current,void**destination,size_t*capacity){
 McSourceLock*t=ctx;if(g_esp!=t->entry+28||!span(t->locked_rect,8)||!identity(t->lock.resource,current))return 0;
 uint32_t desc[7];memcpy(desc,(void*)XBOX_PTR(t->descriptor_address),28);if(memcmp(desc,t->descriptor,28))return 0;
 current->bits=rd(t->locked_rect+4);current->pitch=rd(t->locked_rect);current->format=desc[0];current->width=desc[5];current->height=desc[6];current->caller=t->lock.caller;
 uint32_t resolved=current->data<0x4000000u?current->data|0x80000000u:current->data;
 size_t bytes=(size_t)current->width*current->height*4;uint32_t base,size;uint64_t epoch;
 if(current->bits!=resolved||current->bits<0x80000000u||current->bits>=0x84000000u||!xbox_ContiguousEpoch(current->bits,(uint32_t)bytes,&base,&size,&epoch))return 0;
 current->memory_epoch=epoch;
 *destination=(void*)XBOX_PTR(current->bits);*capacity=bytes;return 1;
}
void mc_source_lock_end(McSourceLock*t){
 if(!enabled||!t->candidate)return;
 Cpu saved;cpu_save(&saved);McLock current;void*dst=NULL;size_t cap=0;memset(&current,0,sizeof current);
 if(!revalidate(t,&current,&dst,&cap))source_failed(5,t);
 else{t->lock.bits=current.bits;t->lock.pitch=current.pitch;t->lock.memory_epoch=current.memory_epoch;int copy=mc_save_source_copy(&t->lock,dst,cap);int retained=copy!=0;if(copy==0)copy=mc_bridge_transfer(&t->lock,revalidate,t);if(diagnostic_enabled&&atomic_fetch_add_explicit(&observed_locks,1,memory_order_relaxed)<16)fprintf(stderr,"[CREATOR-MATERIAL-LOCK] retained=%d result=%d parent=%08x bits=%08x width=%u height=%u mip=%u caller=%08x\n",retained,copy,t->lock.header[5],t->lock.bits,t->lock.width,t->lock.height,t->lock.level,t->lock.caller);if(copy<1)source_failed(6,t);}
 cpu_restore(&saved);
}
