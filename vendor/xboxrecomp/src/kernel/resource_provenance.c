/* GPU executor owned bounded provenance; owns no Vk handles. djultra64. */
#include "resource_provenance.h"
#include "resource_mip_layout.h"
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {RjpRegister r;uint64_t id;int live,surface_live;} Object;
typedef struct {uint64_t serial,epoch,binding,binding_checkpoint;uint32_t pc,color;} Target;
typedef struct {uint64_t serial,target,epoch,allocation,contents,binding;uint32_t color,width,height,pitch,format,native_rgba;} Write;
static Object objects[RJP_OBJECTS];static Target targets[RJP_EVENTS];static Write writes[RJP_WRITES];
/* Exact low guest-heap lifetime bitmap. Resource objects are DWORD aligned
 * in [00010000,04000000), proven by the typed frontend wrappers. Tracking all
 * possible addresses costs2MiB demand-zero BSS; no eviction or arbitrary
 * 512-temporary-wrapper exhaustion can resurrect an older freed alias. */
#define RJP_RESOURCE_LIMIT 0x04000000u
static unsigned char retired_resources[RJP_RESOURCE_LIMIT/4u/8u];
static int resource_address(uint32_t resource){return resource>=0x10000u&&resource<RJP_RESOURCE_LIMIT&&!(resource&3u);}
static int retired(uint32_t resource){if(!resource)return 0;if(!resource_address(resource))return 1;return (retired_resources[resource>>5]>>(resource>>2&7u))&1u;}
static void retire(uint32_t resource){retired_resources[resource>>5]|=(unsigned char)(1u<<(resource>>2&7u));}
static void renew(uint32_t resource){if(resource)retired_resources[resource>>5]&=(unsigned char)~(1u<<(resource>>2&7u));}
static unsigned target_next;static uint64_t sequence,next_id,active_target;
typedef struct {uint32_t address,bytes;uint64_t epoch;uint32_t next;} Touched;
/* Exact process coverage, never evicted. Index links survive host realloc.
 * 65536 records cost at most1.5MiB on the supported64bit host; bucket heads16KiB.
 * Exhaustion or allocation failure permanently invalidates coverage. djultra64. */
#define RJP_TOUCHED_LIMIT 65536u
#define RJP_TOUCHED_BUCKETS 4096u
_Static_assert(sizeof(Touched)<=32u,"Coverage record budget must remain within2MiB");
static Touched*touched;static unsigned touched_count,touched_capacity;
static uint32_t touched_heads[RJP_TOUCHED_BUCKETS];
static int coverage_started,coverage_complete;
static unsigned touched_bucket(uint64_t epoch){
 epoch^=epoch>>33;epoch*=UINT64_C(0xff51afd7ed558ccd);epoch^=epoch>>33;
 return (unsigned)epoch&(RJP_TOUCHED_BUCKETS-1u);
}
static const Touched*touched_overlap(uint32_t address,uint32_t bytes,uint64_t epoch){
 if(!epoch){
  for(unsigned i=0;i<touched_count;i++)if(address<(uint64_t)touched[i].address+touched[i].bytes&&touched[i].address<(uint64_t)address+bytes)return &touched[i];
  return NULL;
 }
 unsigned bucket=touched_bucket(epoch),zero=touched_bucket(0);
 for(unsigned pass=0;pass<(bucket==zero?1u:2u);pass++)
  for(uint32_t link=touched_heads[pass?zero:bucket];link;link=touched[link-1u].next){
   const Touched*t=&touched[link-1u];
   if((!t->epoch||t->epoch==epoch)&&address<(uint64_t)t->address+t->bytes&&t->address<(uint64_t)address+bytes)return t;
  }
 return NULL;
}
static int classify_diagnostics;static unsigned classify_notices,classify_reasons;
static int(*owner_fn)(void);static int broken;static atomic_int tracking_fault;static uint64_t pending_checkpoint;
static int owner(void){return owner_fn&&!broken&&!atomic_load_explicit(&tracking_fault,memory_order_acquire)&&owner_fn();}
static uint64_t next(void){if(sequence==UINT64_MAX){broken=1;return 0;}return ++sequence;}
void rjp_tracking_fault(void){atomic_store_explicit(&tracking_fault,1,memory_order_release);}
int rjp_enabled(void){return owner();}
int rjp_configure(int(*fn)(void)){if(owner_fn||!fn)return 0;const char*d=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");classify_diagnostics=d&&!strcmp(d,"1");owner_fn=fn;return 1;}
int rjp_checkpoint_job(const void*in,uint32_t ni,void*out,uint32_t no){
 (void)in;if(!owner()||ni||!out||no!=sizeof(RjpCheckpoint))return -1;
 if(pending_checkpoint){rjp_tracking_fault();return -1;}
 RjpCheckpoint c={1,0,next()};if(!c.serial)return -1;pending_checkpoint=c.serial;memcpy(out,&c,sizeof c);return 1;
}
static int header_same(const uint32_t*,const uint32_t*,int);
static uint32_t native_format(const Target*t,uint32_t color,uint64_t epoch,uint32_t width,uint32_t height,uint32_t pitch){
 if(!t||t->color!=color||t->epoch!=epoch||!t->binding)return 0;
 for(unsigned i=0;i<RJP_OBJECTS;i++)if(objects[i].live&&objects[i].id==t->binding){const RjpRegister*r=&objects[i].r;
  if(r->resolved_data==color&&r->guest_epoch==epoch&&r->width==width&&r->height==height&&r->pitch==pitch&&(r->format==6u||r->format==18u))return r->format;
 }
 return 0;
}
static int linear_registration(const RjpRegister*r){
 RjpMip m;
 if(r->format!=18u)return 0;
 if(r->parent){if(!rjp_mip_surface(r->parent,r->parent_header,r->header,&m))return 0;}
 else{uint32_t p[5];memcpy(p,r->header,sizeof p);p[0]=(p[0]&~0x70000u)|0x40000u;if(!rjp_mip_at(p,0,&m))return 0;}
 return r->resolved_data==m.bits&&r->width==m.width&&r->height==m.height&&r->pitch==m.pitch;
}
static int valid(const RjpRegister*r){
 return r->version==1&&resource_address(r->resource)&&(!r->parent||resource_address(r->parent))&&((r->header[0]>>16)&7u)==5u&&r->header[5]==r->parent&&r->resource!=r->parent&&(!r->parent||((r->parent_header[0]>>16)&7u)==4u)&&r->resolved_data&&r->guest_epoch&&r->checkpoint&&r->checkpoint<=sequence&&(r->format==6u||linear_registration(r))&&
 ((r->header[3]>>8)&255u)==r->format&&r->header[1]&&r->width&&r->height&&r->width<=512&&r->height<=512&&
 (r->format==18u||(!(r->width&(r->width-1))&&!(r->height&(r->height-1))&&r->pitch==r->width*4))&&
 r->ring_limit>r->ring_base&&r->begin>=r->ring_base&&r->begin<r->ring_limit&&r->end>=r->ring_base&&r->end<=r->ring_limit&&r->begin!=r->end&&
 !(r->begin&3u)&&!(r->end&3u)&&(!r->wrap_tail_end||(r->begin>r->end&&!(r->wrap_tail_end&3u)&&r->wrap_tail_end>r->begin&&r->wrap_tail_end<=r->ring_limit));
}
static int in_span(const RjpRegister*r,uint32_t pc){
 if(pc<r->ring_base||pc>=r->ring_limit)return 0;
 return r->begin<r->end?(pc>=r->begin&&pc<r->end):
  ((pc>=r->begin&&pc<(r->wrap_tail_end?r->wrap_tail_end:r->ring_limit))||pc<r->end);
}
int rjp_register_job(const void*in,uint32_t ni,void*out,uint32_t no){
 if(!owner()||!in||ni!=sizeof(RjpRegister)||!out||no!=sizeof(RjpRegistered))return -1;
 RjpRegister r;memcpy(&r,in,sizeof r);if(!valid(&r)||r.checkpoint!=pending_checkpoint||next_id==UINT64_MAX)return -1;
 unsigned at=RJP_OBJECTS;
 for(unsigned i=0;i<RJP_OBJECTS;i++){
  const Object*o=&objects[i];
  int same_storage=r.parent?(o->r.parent==r.parent&&o->r.resolved_data==r.resolved_data&&o->r.width==r.width&&o->r.height==r.height&&o->r.pitch==r.pitch&&o->r.format==r.format&&o->r.guest_epoch==r.guest_epoch&&header_same(o->r.parent_header,r.parent_header,0)):
     (!o->r.parent&&o->r.resource==r.resource);
  if(o->live&&same_storage){at=i;break;}
  if(!o->live&&at==RJP_OBJECTS)at=i;
 }
 if(at==RJP_OBJECTS)return -1;
 uint64_t id=objects[at].live?objects[at].id:++next_id;objects[at]=(Object){r,id,1,1};
 renew(r.resource);renew(r.parent);
 /* Original commands may already have executed. Tag their exact event/write
  * records, never a mutable global latest-binding guessed from address alone. */
 for(unsigned k=0;k<RJP_EVENTS;k++){
  Target*t=&targets[k];if(t->serial<=r.checkpoint)continue;
  int span=in_span(&r,t->pc);
  if(span&&t->binding_checkpoint<r.checkpoint){
   t->binding=t->color==r.resolved_data&&t->epoch==r.guest_epoch?id:0;
   t->binding_checkpoint=r.checkpoint;
  }
  /* Publish provisional old-command bindings only after the new original
   * SetRT's exact span is known. A lost event remains unattested (binding0). */
  for(unsigned w=0;w<RJP_WRITES;w++)if(writes[w].target==t->serial){
   Write*x=&writes[w];x->binding=t->binding;
   if(x->native_rgba)x->format=native_format(t,x->color,x->epoch,x->width,x->height,x->pitch);
  }
 }
 pending_checkpoint=0;RjpRegistered result={1,0,id};memcpy(out,&result,sizeof result);return 1;
}
int rjp_forget_job(const void*in,uint32_t ni,void*out,uint32_t no){
 if(!owner()||!in||ni!=sizeof(RjpForget)||!out||no!=sizeof(int))return -1;
 RjpForget f;memcpy(&f,in,sizeof f);if(!resource_address(f.resource)||f.reserved)return -1;
 retire(f.resource);
 for(unsigned i=0;i<RJP_OBJECTS;i++)if(objects[i].live){
  if(objects[i].r.parent==f.resource)objects[i].live=0;
  else if(objects[i].r.resource==f.resource){
   if(objects[i].r.parent)objects[i].surface_live=0;
   else objects[i].live=0;
  }
 }
 int result=1;memcpy(out,&result,sizeof result);return 1;
}
int rjp_target(uint32_t pc,uint32_t color,uint64_t epoch){
 if(!owner())return 0;
 active_target=0;
 if(!pc||!color||!epoch)return 0;
 uint64_t serial=next();if(!serial)return 0;
 targets[target_next]=(Target){.serial=serial,.epoch=epoch,.pc=pc,.color=color};
 Target*t=&targets[target_next];
 for(unsigned i=0;i<RJP_OBJECTS;i++)if(objects[i].live){
  const RjpRegister*r=&objects[i].r;int span=in_span(r,pc);
  if(span&&serial>r->checkpoint&&color==r->resolved_data&&epoch==r->guest_epoch&&r->checkpoint>t->binding_checkpoint){t->binding=objects[i].id;t->binding_checkpoint=r->checkpoint;}
 }target_next=(target_next+1)%RJP_EVENTS;active_target=serial;return 1;
}
static const Target*target(uint64_t serial){for(unsigned i=0;i<RJP_EVENTS;i++)if(targets[i].serial==serial)return&targets[i];return 0;}
static int write_rgba(uint32_t color,uint64_t epoch,uint64_t allocation,uint64_t contents,uint32_t width,uint32_t height,uint32_t pitch,uint32_t format,uint32_t native){
 if(!owner()||!active_target||!allocation||!contents||!width||!height||!pitch)return 0;
 const Target*t=target(active_target);if(!t||t->color!=color||t->epoch!=epoch)return 0;
 unsigned at=RJP_WRITES;
 for(unsigned i=0;i<RJP_WRITES;i++){
  if(writes[i].allocation==allocation){at=i;break;}
  if(!writes[i].allocation&&at==RJP_WRITES)at=i;
 }
 if(at==RJP_WRITES)return 0; /* Metadata full: never evict another producer. */
 uint64_t serial=next();if(!serial)return 0;
 uint64_t binding=pending_checkpoint&&t->serial>pending_checkpoint?0:t->binding;
 if(native)format=binding?native_format(t,color,epoch,width,height,pitch):0;
 writes[at]=(Write){serial,t->serial,epoch,allocation,contents,binding,color,width,height,pitch,format,native};return 1;
}

int rjp_write(uint32_t color,uint64_t epoch,uint64_t allocation,uint64_t contents,uint32_t width,uint32_t height,uint32_t pitch,uint32_t format){
 return write_rgba(color,epoch,allocation,contents,width,height,pitch,format,0);
}
int rjp_write_native_rgba(uint32_t color,uint64_t epoch,uint64_t allocation,uint64_t contents,uint32_t width,uint32_t height,uint32_t pitch){
 return write_rgba(color,epoch,allocation,contents,width,height,pitch,0,1);
}

/* Original157A50/157A90/157BF0 mutate internal/public refcounts; Busy[2]
 * is synchronization state, not object identity. Destruction still MUST forget.
 * All TYPE/other Common flags, Data,Format,Size and surface Parent stay exact. */
static int header_same(const uint32_t*a,const uint32_t*b,int surface){
 if((a[0]&~0x0078FFFFu)!=(b[0]&~0x0078FFFFu)||a[1]!=b[1]||a[3]!=b[3]||a[4]!=b[4])return 0;
 return !surface||a[5]==b[5];
}
static int same(const RjpRegister*r,const RjpQuery*q){
 if(r->resolved_data!=q->resolved_data||r->width!=q->width||r->height!=q->height||r->pitch!=q->pitch||r->format!=q->format||r->guest_epoch!=q->guest_epoch)return 0;
 if(r->resource==q->resource)return r->parent==q->parent&&header_same(r->header,q->header,1)&&header_same(r->parent_header,q->parent_header,0);
 /* Lock/GetSurfaceLevel may produce another surface wrapper for the SAME
  * exact parent texture, level0/face0 storage. No synthetic parent lookup. */
 if(r->parent&&q->parent==r->parent&&((q->header[0]>>16)&7u)==5u)
  return header_same(r->header,q->header,1)&&header_same(r->parent_header,q->parent_header,0);
 /* A direct parent texture Lock is also exact via its recorded parent header. */
 if(r->parent&&q->resource==r->parent&&!q->parent&&((q->header[0]>>16)&7u)==4u)
  return header_same(r->parent_header,q->header,0);
 return 0;
}
int rjp_query(const RjpQuery*q,RjpProof*out){
 if(!owner()||pending_checkpoint||!q||!out||!q->allocation||!q->contents)return 0;
 if(retired(q->resource)||retired(q->parent))return 0;
 for(unsigned i=0;i<RJP_OBJECTS;i++)if(objects[i].live&&same(&objects[i].r,q)){
  const Object*o=&objects[i];
  /* Parent storage survives wrapper destruction, but the retired wrapper
   * itself cannot authorize a Lock. A new factory reusing this VA needs an
   * explicit lifecycle registration; address/header equality is insufficient. */
  if(!o->surface_live&&q->resource==o->r.resource)continue;
  const Write*best=0;
  for(unsigned k=0;k<RJP_WRITES;k++){
   const Write*w=&writes[k];
   if(w->binding!=o->id||w->allocation!=q->allocation||w->contents!=q->contents||w->epoch!=q->guest_epoch||w->color!=q->resolved_data||w->width!=q->width||w->height!=q->height||w->pitch!=q->pitch||w->format!=q->format)continue;
   /* Immutable attestation was made at actual target execution/POST.
    * Subsequent unrelated PB history cannot revoke an unchanged parent image. */
   if(w->target&&(!best||w->serial>best->serial))best=w;
  }
  if(best){*out=(RjpProof){o->id,q->guest_epoch,q->allocation,q->contents,best->serial,best->target};return 1;}
 }
 return 0;
}

int rjp_coverage_begin(void){
 if(!owner_fn||sequence||coverage_started||atomic_load_explicit(&tracking_fault,memory_order_acquire))return 0;
 coverage_started=coverage_complete=1;return 1;
}
void rjp_coverage_unknown(void){if(owner())coverage_complete=0;}
void rjp_gpu_written(uint32_t address,uint32_t bytes,uint64_t epoch){
 if(!owner()||!coverage_started)return;
 if(!bytes||!address||(uint64_t)address+bytes>UINT64_C(0x100000000)){coverage_complete=0;return;}
 unsigned bucket=touched_bucket(epoch);
 for(uint32_t link=touched_heads[bucket];link;link=touched[link-1u].next){
  const Touched*t=&touched[link-1u];
  if(t->epoch==epoch&&address>=t->address&&(uint64_t)address+bytes<=(uint64_t)t->address+t->bytes)return;
 }
 if(touched_count==RJP_TOUCHED_LIMIT){coverage_complete=0;return;}
 if(touched_count==touched_capacity){
  unsigned capacity=touched_capacity?touched_capacity*2u:512u;
  if(capacity>RJP_TOUCHED_LIMIT)capacity=RJP_TOUCHED_LIMIT;
  Touched*grown=(Touched*)realloc(touched,(size_t)capacity*sizeof(*touched));
  if(!grown){coverage_complete=0;return;}
  touched=grown;touched_capacity=capacity;
 }
 touched[touched_count]=(Touched){address,bytes,epoch,touched_heads[bucket]};
 touched_heads[bucket]=++touched_count;
}
/* Owner-only bounded observation. No new guest reads, admission or coverage
 * transitions; own diagnostic work: djultra64. */
static void classify_unknown_notice(uint32_t address,uint32_t bytes,uint64_t epoch,const char*reason){
 if(!classify_diagnostics||classify_notices>=8)return;
 unsigned reason_bit=!strcmp(reason,"pending-checkpoint")?1u:!strcmp(reason,"coverage-not-started")?2u:!strcmp(reason,"coverage-incomplete")?4u:!strcmp(reason,"invalid-range")?8u:16u;
 if(classify_reasons&reason_bit)return;
 classify_reasons|=reason_bit;
 const Touched*overlap=NULL;
 for(unsigned i=0;i<touched_count;i++)if((!epoch||!touched[i].epoch||epoch==touched[i].epoch)&&address<(uint64_t)touched[i].address+touched[i].bytes&&touched[i].address<(uint64_t)address+bytes){overlap=&touched[i];break;}
 fprintf(stderr,"[RJP-CPU-UNKNOWN] n=%u reason=%s coverage_started=%d coverage_complete=%d touched_count=%u pending_checkpoint=%llu address=%08x bytes=%u epoch=%llu overlap=%u overlap_address=%08x overlap_bytes=%u overlap_epoch=%llu\n",
  ++classify_notices,reason,coverage_started,coverage_complete,touched_count,(unsigned long long)pending_checkpoint,address,bytes,(unsigned long long)epoch,overlap!=NULL,overlap?overlap->address:0,overlap?overlap->bytes:0,(unsigned long long)(overlap?overlap->epoch:0));
}
int rjp_cpu_classify(uint32_t address,uint32_t bytes,uint64_t epoch){
 int owned=owner();
 if(!owned||pending_checkpoint||!coverage_started||!coverage_complete||!address||!bytes||(uint64_t)address+bytes>UINT64_C(0x100000000)){
  if(owned)classify_unknown_notice(address,bytes,epoch,pending_checkpoint?"pending-checkpoint":!coverage_started?"coverage-not-started":!coverage_complete?"coverage-incomplete":"invalid-range");
  return -1;
 }
 if(touched_overlap(address,bytes,epoch)){classify_unknown_notice(address,bytes,epoch,"gpu-overlap");return -1;}
 return 0;
}

int rjp_query_diagnostic(const RjpQuery*q,RjpQueryDiagnostic*out){
 if(!owner_fn||!owner_fn()||!q||!out)return 0;
 RjpQueryDiagnostic d={0};d.version=1;d.broken=broken;
 d.tracking_fault=atomic_load_explicit(&tracking_fault,memory_order_acquire);
 d.pending_checkpoint=pending_checkpoint;d.retired_resource=retired(q->resource);d.retired_parent=retired(q->parent);
 unsigned object_score=0,write_score=0;
 for(unsigned i=0;i<RJP_OBJECTS;i++){
  const Object*o=&objects[i];if(o->live)d.live_objects++;
  unsigned score=(o->r.parent&&o->r.parent==q->resource)?4u:o->r.resource==q->resource?3u:o->r.resolved_data==q->resolved_data?1u:0u;
  if(!score)continue;
  d.related_objects++;int matches=same(&o->r,q);if(o->live&&matches)d.same_objects++;
  if(o->live&&matches)score+=8;
  if(score>object_score){object_score=score;d.object=o->r;d.object_id=o->id;d.object_live=o->live;d.surface_live=o->surface_live;d.object_same=matches;}
 }
 for(unsigned i=0;i<RJP_WRITES;i++){
  const Write*w=&writes[i];if(w->allocation)d.writes_used++;
  if(w->allocation==q->allocation)d.allocation_writes++;
  if(w->color==q->resolved_data)d.color_writes++;
  unsigned score=w->allocation==q->allocation?4u:w->color==q->resolved_data?1u:0u;
  if(score&&(score>write_score||(score==write_score&&w->serial>d.write_serial))){
   write_score=score;d.write_serial=w->serial;d.write_target=w->target;d.write_epoch=w->epoch;
   d.write_allocation=w->allocation;d.write_contents=w->contents;d.write_binding=w->binding;
   d.write_color=w->color;d.write_width=w->width;d.write_height=w->height;d.write_pitch=w->pitch;d.write_format=w->format;
  }
 }
 *out=d;return 1;
}

int rjp_forget_allocation(uint64_t allocation){
 if(!owner()||!allocation)return 0;
 for(unsigned i=0;i<RJP_WRITES;i++)if(writes[i].allocation==allocation)memset(&writes[i],0,sizeof writes[i]);
 return 1;
}
