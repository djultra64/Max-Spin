/* Mutex orders copied emission before exposing the original PUT. djultra64. */
#include "visual_pb_bridge.h"
#include <string.h>
#include <limits.h>
static _Thread_local VpliBridge*inside;
static void first_failure(VpliBridge*b,unsigned why,unsigned line){
 if(!b->first_failure.version){
  VpliFailure*f=&b->first_failure;f->reason=why;f->source_line=line;
  f->ledger_status=b->ledger.status;f->head=b->ledger.head;f->count=b->ledger.count;
  f->producer=b->ledger.producer;f->remaining=b->remaining;f->next_method=b->next_method;
  f->draw_active=b->draw_active;f->wrap=b->ledger.wrap;f->packet=b->packet;f->version=1;
 }
}
static int lock(VpliBridge*b){if(!b||!b->enabled||b->failed)return 0;if(inside==b){first_failure(b,VPL_ORDER,__LINE__);b->failed=1;return 0;}pthread_mutex_lock(&b->mutex);inside=b;if(b->failed){inside=NULL;pthread_mutex_unlock(&b->mutex);return 0;}return 1;}
static void unlock(VpliBridge*b){inside=NULL;pthread_mutex_unlock(&b->mutex);}
static int fail_at(VpliBridge*b,unsigned why,unsigned line){first_failure(b,why,line);b->failed=1;if(b->config.fault)b->config.fault(b->config.user,why);return 0;}
#define fail(b,why) fail_at(b,why,__LINE__)
static int same_scene(const VpliSceneTag*a,const VpliSceneTag*b){return a->version==b->version&&a->reserved==b->reserved&&a->simulation_tick==b->simulation_tick&&a->simulation_time_bits==b->simulation_time_bits&&a->scene==b->scene&&a->view==b->view&&a->authority_revision==b->authority_revision&&a->boundary_ns==b->boundary_ns;}
static int valid_scene(const VpliSceneTag*s){return s&&s->version==VPLI_SCENE_TAG_VERSION&&!s->reserved&&s->scene&&s->view&&s->authority_revision&&s->boundary_ns;}
static int consume_word(VpliBridge*b,uint32_t at,uint32_t word,VplSpan*span,VpliSceneTag*tag){
 if(!b->ledger.count)return fail(b,VPL_ORDER);uint32_t head=b->ledger.head,count=b->ledger.count;
 *tag=b->interval_scene[head];if(!vpl_consume(&b->ledger,at,word,span))return 0;
 if(b->ledger.count<count){if(b->interval_cut[head]){if(!b->scene_cuts)return fail(b,VPL_ORDER);b->scene_cuts--;}b->interval_cut[head]=0;memset(&b->interval_scene[head],0,sizeof b->interval_scene[head]);}
 return 1;
}
static int draw_word(VpliBridge*b,const VplSpan*s,const VpliSceneTag*tag){
 if(!b->draw_active)return 1;if(!same_scene(tag,&b->draw_scene))return fail(b,VPL_ORDER);
 if(b->draw_wrap_pending){
  const VplSpan*w=&b->draw_wrap_source;
  if(s->identity!=w->identity||s->epoch!=w->epoch||w->wrap_generation==UINT64_MAX||s->wrap_generation!=w->wrap_generation+1||s->begin!=b->ledger.ring_begin)return fail(b,VPL_ORDER);
  b->draw_wrap_pending=0;
 }
 if(b->draw_discard)return 1;
 if(s->identity!=b->draw_span.identity||s->epoch!=b->draw_span.epoch||s->wrap_generation!=b->draw_span.wrap_generation||s->begin!=b->draw_span.end)return fail(b,VPL_ORDER);
 b->draw_span.end=s->end;return 1;
}
/* Joining is authorized only by the immutable full tag, actual allocation
 * identity, epoch, observed wrap, and exact contiguity; never latest-scene. */
static uint32_t append_room(VpliBridge*b){
 VplLedger*l=&b->ledger;if(!l->count)return 0;uint32_t slot=(l->head+l->count-1u)%VPL_INTERVALS;VplInterval*q=&l->intervals[slot];
 if(!same_scene(&b->interval_scene[slot],&b->staged_scene)||q->span.identity!=(((uint64_t)l->base<<32)|l->size)||q->span.epoch!=l->epoch||q->span.wrap_generation!=l->wrap||q->span.end!=l->producer||q->used>VPL_BYTES)return 0;
 return VPL_BYTES-q->used;
}
static int new_cut(VpliBridge*b){if(!b->ledger.count)return 0;return !same_scene(&b->interval_scene[(b->ledger.head+b->ledger.count-1u)%VPL_INTERVALS],&b->staged_scene);}
static void tag_tail(VpliBridge*b,int cut){uint32_t slot=(b->ledger.head+b->ledger.count-1u)%VPL_INTERVALS;b->interval_scene[slot]=b->staged_scene;b->interval_cut[slot]=(uint32_t)cut;b->scene_cuts+=(uint32_t)cut;}
static int publish_until(VpliBridge*b,uint32_t end){
 if(end<b->ledger.producer||(end&3))return fail(b,VPL_ORDER);
 while(b->ledger.producer<end){uint32_t room=append_room(b),n=end-b->ledger.producer;int append=room!=0,cut=append?0:new_cut(b);
  if(cut&&b->scene_cuts>=VPL_TAG_CUTS){b->ledger.status=VPL_BUDGET;return fail(b,VPL_BUDGET);}
  uint32_t limit=append?room:VPL_BYTES;if(n>limit)n=limit;uint32_t next=b->ledger.producer+n;
  int ok=append?vpl_append(&b->ledger,b->ledger.producer,next,NULL):vpl_publish(&b->ledger,b->ledger.producer,next,NULL);
  if(!ok)return fail(b,b->ledger.status);if(!append)tag_tail(b,cut);
 }
 return 1;
}
static int read32(VpliBridge*b,uint32_t va,uint32_t*out){return b->config.control_read&&b->config.control_read(b->config.user,va,out);}
static int attach(VpliBridge*b,uint32_t target){
 if(b->ledger.initialized){uint32_t current,begin,end,canonical,base,size;uint64_t epoch;if(b->ledger.status!=VPL_OK||!read32(b,0x1b9438,&current)||current!=b->device||!read32(b,current+0x24,&begin)||!read32(b,current+0x28,&end)||!b->config.memory.allocation(b->config.memory.user,begin,4,&canonical,&base,&size,&epoch)||canonical!=b->ledger.ring_begin||base!=b->ledger.base||size!=b->ledger.size||epoch!=b->ledger.epoch||end!=b->ledger.ring_end)return fail(b,VPL_EPOCH);return 1;}
 uint32_t device,aperture,begin,end,old;
 if(!read32(b,0x1b9438,&device)||device<0x10000u||device>0x4000000u-0x1c1c||!read32(b,device+0x1c10,&aperture)||aperture!=0xfd800000u||target!=aperture+0x40||!read32(b,device+0x24,&begin)||!read32(b,device+0x28,&end)||!read32(b,target,&old))return fail(b,VPL_INVALID);
 b->device=device;return vpl_begin(&b->ledger,&b->config.memory,begin,end,old)?1:fail(b,VPL_EPOCH);
}
/* Called only while holding the producer/consumer mutex. No byte read. */
static int candidate(VpliBridge*b,uint32_t target,uint32_t value){
 uint32_t device,aperture,begin,end,a,base,size,last,lb,ls;uint64_t epoch,le;
 if(!read32(b,0x1b9438,&device)||device<0x10000u||device>0x4000000u-0x1c1c||
 !read32(b,device+0x1c10,&aperture)||aperture!=0xfd800000u||target!=aperture+0x40||
 !read32(b,device+0x24,&begin)||!read32(b,device+0x28,&end)||begin>=end||((begin|end|value)&3)||
 !b->config.memory.allocation(b->config.memory.user,begin,4,&a,&base,&size,&epoch)||a!=begin||!epoch||
 !b->config.memory.allocation(b->config.memory.user,end-4,4,&last,&lb,&ls,&le)||lb!=base||ls!=size||le!=epoch||last!=end-4||
 !b->config.memory.allocation(b->config.memory.user,value,4,&a,&lb,&ls,&le)||lb!=base||ls!=size||le!=epoch||a<begin||a>=end)
 return fail(b,VPL_EPOCH);
 b->candidate_valid=1;b->candidate_device=device;b->candidate_begin=begin;b->candidate_end=end;
 b->candidate_put=a;b->candidate_base=base;b->candidate_size=size;b->candidate_epoch=epoch;return 1;
}
int vpli_ack_initial_begin(VpliBridge*b){
 if(!b||!b->enabled)return 0;if(!lock(b))return 0;
 if(!b->config.decoder_owner(b->config.user)){fail(b,VPL_ORDER);unlock(b);return 0;}
 if(b->ledger.initialized){fail(b,VPL_ORDER);unlock(b);return 0;}return 1;
}
int vpli_ack_initial_commit(VpliBridge*b,uint32_t p){
 /* No guest control reads: only the actual ack's original p and the immutable
  * candidate plus a fresh allocation-incarnation query on the GPU owner. */
 if(!b||inside!=b)return 0;int ok=0;
 if(!p){ok=1;goto done;}
 uint32_t a,base,size;uint64_t epoch;
 if(!b->candidate_valid||!b->config.memory.allocation(b->config.memory.user,p,4,&a,&base,&size,&epoch)||
 a!=b->candidate_put||base!=b->candidate_base||size!=b->candidate_size||epoch!=b->candidate_epoch){fail(b,VPL_EPOCH);goto done;}
 b->device=b->candidate_device;
 ok=vpl_begin(&b->ledger,&b->config.memory,b->candidate_begin,b->candidate_end,p);
 if(!ok)fail(b,VPL_EPOCH);
done:unlock(b);return ok;
}
int vpli_configure(VpliBridge*b,const VpliConfig*c,int enabled){
 if(!b||b->configured||!c||(enabled!=0&&enabled!=1))return 0;
 if(enabled&&(c->memory.enabled!=1||!c->decoder_owner||!c->control_read||!c->fault))return 0;
 if(pthread_mutex_init(&b->mutex,NULL))return 0;b->config=*c;b->configured=1;b->enabled=enabled;return 1;
}
void vpli_store_put(VpliBridge*b,uint32_t target,uint32_t value,void(*store)(void*,uint32_t,uint32_t),void*user){
 if(!store)return;if(!b||!b->enabled){store(user,target,value);return;}
 /* Dummy1B8F80 is not hardware publication. Its original write still occurs. */
 if(target!=0xfd800040u){store(user,target,value);return;}
 int held=lock(b);
 if(held){if(!b->ledger.initialized){(void)candidate(b,target,value);}
 else if(attach(b,target)){uint32_t canonical,base,size;uint64_t epoch;
  if(!b->config.memory.allocation(b->config.memory.user,value,1,&canonical,&base,&size,&epoch)||base!=b->ledger.base||size!=b->ledger.size||epoch!=b->ledger.epoch)fail(b,VPL_EPOCH);
  else if(canonical!=b->ledger.producer)(void)publish_until(b,canonical);
 }}
 /* This exact store is the release/exposure point to the actual ack reader;
  * copied intervals and their epochs already exist under the same mutex. */
 store(user,target,value);if(held)unlock(b);
}
int vpli_observe_wrap(VpliBridge*b,uint32_t at,uint32_t word,uint32_t reset){
 if(!lock(b))return 0;int ok=0;
 if(!b->ledger.initialized){ok=1;goto done;}
 if(at!=b->ledger.producer&&!publish_until(b,at))goto done;
 int append=append_room(b)>=4u,cut=append?0:new_cut(b);
 if(cut&&b->scene_cuts>=VPL_TAG_CUTS){b->ledger.status=VPL_BUDGET;fail(b,VPL_BUDGET);goto done;}
 ok=append?vpl_wrap_append(&b->ledger,at,word,reset):vpl_wrap(&b->ledger,at,word,reset);
 if(!ok)fail(b,b->ledger.status);else if(!append)tag_tail(b,cut);
done:unlock(b);return ok;
}
int vpli_cursor(VpliBridge*b,uint32_t put,VplSpan*out){if(!out||!lock(b))return 0;int ok=0;uint32_t a,base,size;uint64_t epoch;
 if(!b->ledger.initialized){unlock(b);return 0;}
 if(b->ledger.initialized&&b->config.memory.allocation(b->config.memory.user,put,4,&a,&base,&size,&epoch)&&base==b->ledger.base&&size==b->ledger.size&&epoch==b->ledger.epoch&&!(a&3)&&a>=b->ledger.ring_begin&&a<b->ledger.ring_end){*out=(VplSpan){((uint64_t)base<<32)|size,epoch,b->ledger.wrap,a,a};ok=1;}
 else fail(b,VPL_EPOCH);unlock(b);return ok;
}
static int header_locked(VpliBridge*b,uint32_t va,uint32_t word){VplSpan source;VpliSceneTag tag;int ok=0;
 b->draw_ready=0;
 if(b->remaining||!consume_word(b,va,word,&source,&tag)||!draw_word(b,&source,&tag)){fail(b,VPL_ORDER);goto done;}
 /* Transfers/non-method packets need a separate ordered CALL/RETURN codec.
  * Real wrap JUMP is consumed here, but never forged into method provenance. */
 if((word&3u)==1u){
  if(b->draw_active){
   if(word!=((b->ledger.ring_begin&0x0fffffffu)|1u))return fail(b,VPL_ORDER);
   b->draw_wrap_source=source;b->draw_wrap_pending=1;
   if(!b->draw_discard){
    if(b->discarded_draws==UINT64_MAX)return fail(b,VPL_BUDGET);
    b->discarded_draws++;b->draw_discard=1;b->draw_ready=0;
    if(!b->first_wrapped_draw.version){b->first_wrapped_draw.prefix=b->draw_span;b->first_wrapped_draw.transfer=source;b->first_wrapped_draw.scene=b->draw_scene;b->first_wrapped_draw.version=1;}
    /* Semantic coverage failure only: actual byte/packet ledger remains exact.
     * Existing runtime fault sequence invalidates the WHOLE original frame. */
    if(b->config.fault)b->config.fault(b->config.user,VPL_ORDER);
   }
  }
  memset(&b->packet,0,sizeof b->packet);ok=1;goto done;
 }
 if((word&0xe0030003u)!=0&&(word&0xe0030003u)!=0x40000000u){fail(b,VPL_INVALID);goto done;}
 b->next_method=word&0x1ffc;b->remaining=(word>>18)&0x7ff;b->noninc=(word&0xe0000000u)==0x40000000u;b->packet=(VpliPacket){VPLI_PACKET_VERSION,word,(word>>13)&7,word&0x1ffc,source,tag};ok=1;
done:return ok;
}
static int argument_locked(VpliBridge*b,uint32_t va,uint32_t word,VpliSceneTag*bound){VplSpan source;VpliSceneTag tag;int ok=0;
 if(!b->remaining||!consume_word(b,va,word,&source,&tag)||!same_scene(&tag,&b->packet.scene)||source.identity!=b->packet.packet.identity||source.epoch!=b->packet.packet.epoch||source.wrap_generation!=b->packet.packet.wrap_generation||source.begin!=b->packet.packet.end){fail(b,VPL_ORDER);goto done;}
 if(!draw_word(b,&source,&tag))goto done;
 b->packet.packet.end=source.end;b->packet.method=b->next_method;if(!b->noninc)b->next_method+=4;b->remaining--;
 if(b->packet.method==0x17fc){if(word){
  if(!b->draw_discard){b->draw_active=1;b->draw_span=b->packet.packet;b->draw_scene=tag;}b->draw_ready=0;
 }else {b->draw_ready=b->draw_active&&!b->draw_discard;b->draw_active=0;b->draw_discard=0;b->draw_wrap_pending=0;}}
 ok=1;
done:if(ok){b->consumed_scene=tag;*bound=tag;}return ok;
}
/* Existing externally-observed entry points retain their exact callback order. */
int vpli_header(VpliBridge*b,uint32_t va,uint32_t word){
 if(!lock(b))return 0;int ok=0;
 if(!b->config.decoder_owner(b->config.user))fail(b,VPL_ORDER);else ok=header_locked(b,va,word);
 unlock(b);return ok;
}
int vpli_argument(VpliBridge*b,uint32_t va,uint32_t word){
 if(!lock(b))return 0;int ok=0;VpliSceneTag tag={0};
 if(!b->config.decoder_owner(b->config.user))fail(b,VPL_ORDER);else ok=argument_locked(b,va,word,&tag);
 unlock(b);if(ok&&b->config.bind_scene)b->config.bind_scene(b->config.user,&tag);return ok;
}
/* Pair actual raw read with byte consumption under producer/writer mutex.
 * Callback is exactly original nv2a_pb_read: no retained pointer or replay read.
 * Capture failure never skips/repeats the original read or changes its value. */
static int read_observed(VpliBridge*b,uint32_t va,VpliRawRead read,uint32_t*out,int argument){
 if(!read||!out)return 0;
 if(!lock(b)){*out=read(va);return 0;}
 int owner=b->config.decoder_owner(b->config.user)==1;VpliSceneTag tag={0};
 *out=read(va);int ok=0;
 if(!owner)fail(b,VPL_ORDER);
 else if(!b->failed)ok=argument?argument_locked(b,va,*out,&tag):header_locked(b,va,*out);
 unlock(b);if(ok&&argument&&b->config.bind_scene)b->config.bind_scene(b->config.user,&tag);return ok;
}
int vpli_read_header(VpliBridge*b,uint32_t va,VpliRawRead read,uint32_t*out){return read_observed(b,va,read,out,0);}
int vpli_read_argument(VpliBridge*b,uint32_t va,VpliRawRead read,uint32_t*out){return read_observed(b,va,read,out,1);}
int vpli_packet(VpliBridge*b,VpliPacket*out){if(!out||!lock(b))return 0;int ok=b->config.decoder_owner(b->config.user)&&b->packet.version&&b->packet.packet.end>b->packet.packet.begin+4;if(ok)*out=b->packet;unlock(b);return ok;}

int vpli_draw_packet(VpliBridge*b,VpliPacket*out){if(!out||!lock(b))return 0;int ok=b->config.decoder_owner(b->config.user)&&b->draw_ready&&b->packet.method==0x17fc&&vpl_contains(&b->draw_span,&b->packet.packet);if(ok){*out=b->packet;out->packet=b->draw_span;}unlock(b);return ok;}

int vpli_stage_scene(VpliBridge*b,const VpliSceneTag*tag){if(!tag||!lock(b))return 0;int ok=0;uint32_t raw,canonical,base,size;uint64_t epoch;
 if(!b->config.bind_scene||!b->config.authority_revision){fail(b,VPL_INVALID);goto done;}
 /* Startup stage is owned metadata only. The actual ack adoption must arm
  * the cursor; staging never guesses an initial PUT or reads control RAM. */
 if(!b->ledger.initialized){
  if(valid_scene(tag)){b->staged_scene=*tag;ok=1;}
  else if(!tag->version){memset(&b->staged_scene,0,sizeof b->staged_scene);ok=1;}
  else fail(b,VPL_INVALID);goto done;
 }
 /* Preserve emitted-but-not-kicked bytes under their OLD tag before staging
  * the next CPU boundary. This copies only; it does not expose PUT or drain. */
 if(!attach(b,0xfd800040u)||!read32(b,b->device,&raw)||!b->config.memory.allocation(b->config.memory.user,raw,1,&canonical,&base,&size,&epoch)||base!=b->ledger.base||size!=b->ledger.size||epoch!=b->ledger.epoch){fail(b,VPL_EPOCH);goto done;}
 if(!publish_until(b,canonical))goto done;
 if(valid_scene(tag)){b->staged_scene=*tag;ok=1;}
 else if(!tag->version){memset(&b->staged_scene,0,sizeof b->staged_scene);ok=1;}
 else fail(b,VPL_INVALID);
done:unlock(b);return ok;}

int vpli_consumed_scene(VpliBridge*b,VpliSceneTag*out){if(!out||!lock(b))return 0;int ok=b->config.decoder_owner(b->config.user)&&valid_scene(&b->consumed_scene)&&b->config.authority_revision&&b->config.authority_revision(b->config.user)==b->consumed_scene.authority_revision;if(b->failed)ok=0;if(ok)*out=b->consumed_scene;else memset(out,0,sizeof*out);unlock(b);return ok;}

/* Exact original fence rewrite authority, never a generic ledger refresh. */
int vpli_fence_birth(VpliBridge*b,uint32_t record,uint32_t fence){
 if(!b||!b->enabled)return 1;if(!lock(b))return 0;int ok=0;
 uint32_t a,base,size;uint64_t epoch;
 if((record&3)||!b->config.memory.allocation(b->config.memory.user,record,32,&a,&base,&size,&epoch)||!epoch||b->fence_birth==UINT64_MAX){fail(b,VPL_EPOCH);goto done;}
 if(b->ledger.initialized&&(base!=b->ledger.base||size!=b->ledger.size||epoch!=b->ledger.epoch||a<b->ledger.ring_begin||(uint64_t)a+32>b->ledger.ring_end)){fail(b,VPL_EPOCH);goto done;}
 VpliFence*f=&b->fences[(fence>>1)&63u];*f=(VpliFence){++b->fence_birth,epoch,b->ledger.initialized?b->ledger.wrap:0,a,fence,base,size,{0x41d90,0,0x41d90,0}};ok=1;
 done:unlock(b);return ok;
}
static int fence_pending(VpliBridge*b,VpliFence*f,unsigned first,unsigned pairs,const uint32_t*after){
 VplLedger*l=&b->ledger;if(!l->initialized)return 1;
 /* Validate the complete transaction before changing a single owned byte. */
 unsigned char*dest[4]={0};
 for(unsigned p=0;p<pairs;p++){
  unsigned index=first+p*2;uint32_t at=f->record+16+index*4;unsigned found=0;
  for(unsigned k=0;k<2;k++){
   uint32_t va=at+k*4;
   for(unsigned n=0;n<l->count;n++){
    VplInterval*q=&l->intervals[(l->head+n)%VPL_INTERVALS];
    if(va>=q->span.begin&&va<q->span.end&&va>=q->span.begin+q->consumed){
     if(q->span.epoch!=f->epoch||q->span.identity!=(((uint64_t)f->base<<32)|f->size)||q->span.wrap_generation!=f->wrap)return fail(b,VPL_EPOCH);
     uint32_t old;unsigned char*ptr=q->bytes+(va-q->span.begin);memcpy(&old,ptr,4);
     if(old!=f->words[index+k])return fail(b,VPL_BYTES_CHANGED);
     dest[p*2+k]=ptr;found++;break;
    }
   }
  }
  /* An already consumed header with an unread argument cannot be relabeled. */
  if(found==1)return fail(b,VPL_ORDER);
 }
 for(unsigned k=0;k<pairs*2;k++)if(dest[k])memcpy(dest[k],&after[k],4);
 return 1;
}
void vpli_fence_patch(VpliBridge*b,uint32_t phase,uint32_t record,uint32_t fence,
 void(*stores)(void*,uint32_t,uint32_t),void*user){
 if(!stores)return;if(!b||!b->enabled){stores(user,phase,record);return;}
 int held=lock(b);
 if(held){
  uint32_t a,base,size;uint64_t epoch;VpliFence*f=&b->fences[(fence>>1)&63u];
  if(phase<VPLI_FENCE_WAIT||phase>VPLI_FENCE_RESTORE||!f->birth||f->fence!=fence||
   !b->config.memory.allocation(b->config.memory.user,record,32,&a,&base,&size,&epoch)||a!=f->record||base!=f->base||size!=f->size||epoch!=f->epoch||
   (b->ledger.initialized&&(epoch!=b->ledger.epoch||base!=b->ledger.base||size!=b->ledger.size)))fail(b,VPL_EPOCH);
  else{
   unsigned first=phase==VPLI_FENCE_TRAP?2u:0u,pairs=phase==VPLI_FENCE_RESTORE?2u:1u;
   uint32_t after[4]={phase==VPLI_FENCE_WAIT?0x40110u:0x40100u,phase==VPLI_FENCE_TRAP?5u:0u,0x40100u,0};
   if(fence_pending(b,f,first,pairs,after))memcpy(f->words+first,after,pairs*8u);
  }
 }
 /* Original straight-line guest writes exactly once under the same mutex as
  * the decoder's actual read+consume; no callback may reenter this bridge. */
 stores(user,phase,record);if(held)unlock(b);
}
