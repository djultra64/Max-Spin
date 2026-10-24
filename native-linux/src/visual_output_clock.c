/* Host visual deadlines and immutable frame leases. Own work: djultra64. */
#include "visual_output_clock.h"
#include <stdlib.h>
#include <string.h>
typedef struct {const void*frame;void*lease;VocFrameInfo info;} Held;
typedef struct {uint64_t device,scene,view,domain,a,b,n,d;} Key;
struct VocClock {
 VocProvider p;Held previous,current;Key pending,last;
 uint64_t anchor,ordinal,last_now,nonce,ticket;
 uint32_t hz;int entering,inflight,has_last;
};
static int rate_ok(uint32_t r){return !r||r==60||r==90||r==120;}
static int enter(VocClock*c){if(!c)return VOC_INVALID;if(c->entering)return VOC_BUSY;c->entering=1;if(!c->p.owner(c->p.user)){c->entering=0;return VOC_OWNER;}return 1;}
static int leave(VocClock*c,int result){c->entering=0;return result;}
static void drop(VocClock*c,Held*h){if(h->lease)c->p.release(c->p.user,h->frame,h->lease);memset(h,0,sizeof*h);}
static int context(VocFrameInfo a,VocFrameInfo b){return a.device==b.device&&a.scene==b.scene&&a.view==b.view&&a.domain==b.domain;}
static int info_ok(VocFrameInfo i){return i.device&&i.scene&&i.view&&i.domain&&i.serial&&(i.flags&VOC_COMPLETE)&&!(i.flags&~(VOC_COMPLETE|VOC_INTERPOLABLE));}
static int equal(VocFrameInfo a,VocFrameInfo b){return context(a,b)&&a.serial==b.serial&&a.step==b.step&&a.sample_ns==b.sample_ns&&a.published_ns==b.published_ns&&a.flags==b.flags;}
static int valid(VocClock*c,Held*h){VocFrameInfo i={0};return h->lease&&c->p.inspect(c->p.user,h->frame,h->lease,&i)&&info_ok(i)&&equal(i,h->info);}
static int pair(VocClock*c){
 VocFrameInfo a=c->previous.info,b=c->current.info;
 return c->previous.lease&&context(a,b)&&(a.flags&b.flags&VOC_INTERPOLABLE)&&
  a.step!=UINT64_MAX&&b.step==a.step+1&&b.serial>a.serial&&b.sample_ns>a.sample_ns;
}
static Key endpoint(VocFrameInfo i){return(Key){i.device,i.scene,i.view,i.domain,i.serial,i.serial,1,1};}
static int key_equal(Key a,Key b){return a.device==b.device&&a.scene==b.scene&&a.view==b.view&&a.domain==b.domain&&a.a==b.a&&a.b==b.b&&a.n==b.n&&a.d==b.d;}
/* floor(n*1e9/hz) avoids cumulative rounded-period drift at90Hz. */
static int deadline(VocClock*c,uint64_t n,uint64_t*out){
 __uint128_t d=(__uint128_t)n*1000000000u/c->hz+c->anchor;
 if(d>UINT64_MAX)return 0;
 *out=(uint64_t)d;return 1;
}
static int advance(VocClock*c,uint64_t now,uint64_t*wake){
 if(!c->hz){*wake=UINT64_MAX;return 1;}
 __uint128_t elapsed=(__uint128_t)(now-c->anchor)*c->hz/1000000000u;
 if(c->ordinal==UINT64_MAX||elapsed>=UINT64_MAX)return 0;
 uint64_t next=c->ordinal+1;if(next<elapsed+1)next=(uint64_t)elapsed+1;
 if(!deadline(c,next,wake))return 0;
 c->ordinal=next;return 1;
}
int voc_create(const VocProvider*p,uint32_t rate,uint64_t now,VocClock**out){
 if(!out)return VOC_INVALID;
 *out=NULL;
 if(!p||!rate_ok(rate)||!p->owner||!p->retain||!p->inspect||!p->release)return VOC_INVALID;
 if(!p->owner(p->user))return VOC_OWNER;
 VocClock*c=calloc(1,sizeof*c);if(!c)return VOC_INVALID;
 c->p=*p;c->hz=rate;c->anchor=c->last_now=now;*out=c;return 1;
}
int voc_offer(VocClock*c,const void*frame){
 int e=enter(c);if(e<0)return e;if(c->inflight)return leave(c,VOC_BUSY);
 if(!frame)return leave(c,VOC_INVALID);
 Held h={frame,NULL,{0}};
 if(!c->p.retain(c->p.user,frame,&h.lease)||!h.lease)return leave(c,VOC_STALE);
 if(!c->p.inspect(c->p.user,frame,h.lease,&h.info)||!info_ok(h.info)){drop(c,&h);return leave(c,VOC_STALE);}
 if(c->current.lease&&context(c->current.info,h.info)){
  VocFrameInfo a=c->current.info,b=h.info;
  if(equal(a,b)&&frame==c->current.frame){drop(c,&h);return leave(c,VOC_WAIT);}
  if(b.serial<=a.serial||b.step<a.step||b.sample_ns<a.sample_ns||b.published_ns<a.published_ns){drop(c,&h);return leave(c,VOC_INVALID);}
 }else drop(c,&c->current);
 drop(c,&c->previous);c->previous=c->current;c->current=h;
 if(!pair(c))drop(c,&c->previous);
 return leave(c,1);
}
int voc_poll(VocClock*c,uint64_t now,VocSample*out,uint64_t*wake){
 int e=enter(c);if(e<0)return e;
 if(!out||!wake)return leave(c,VOC_INVALID);
 *wake=UINT64_MAX;memset(out,0,sizeof*out);
 if(c->inflight)return leave(c,VOC_BUSY);
 if(now<c->last_now||now<c->anchor)return leave(c,VOC_INVALID);
 c->last_now=now;
 if(!c->current.lease)return leave(c,VOC_WAIT);
 if(!valid(c,&c->current)){drop(c,&c->previous);drop(c,&c->current);return leave(c,VOC_STALE);}
 if(now<c->current.info.published_ns)return leave(c,VOC_INVALID);
 uint64_t due;if(c->hz){if(!deadline(c,c->ordinal,&due))return leave(c,VOC_OVERFLOW);if(now<due){*wake=due;return leave(c,VOC_WAIT);}}
 if(c->previous.lease&&!valid(c,&c->previous))drop(c,&c->previous);
 const Held*a=&c->current,*b=&c->current;uint64_t n=1,d=1;Key k=endpoint(b->info);
 if(pair(c)){
  a=&c->previous;d=b->info.sample_ns-a->info.sample_ns;n=now-b->info.published_ns;if(n>d)n=d;
  if(!n)k=endpoint(a->info);
  else if(n==d)k=endpoint(b->info);
  else k=(Key){b->info.device,b->info.scene,b->info.view,b->info.domain,a->info.serial,b->info.serial,n,d};
 }
 if(!advance(c,now,wake))return leave(c,VOC_OVERFLOW);
 if(c->has_last&&key_equal(k,c->last)){
  /* Unlimited can advance a valid pair even without another source frame.
   * UINT64_MAX here would stall after a shared endpoint until the next offer.
   * This is a wake deadline for the consumer, never a spin or guest update. */
  if(!c->hz&&a!=b&&n<d&&now!=UINT64_MAX)*wake=now+1;
  return leave(c,VOC_WAIT);
 }
 if(c->nonce==UINT64_MAX)return leave(c,VOC_OVERFLOW);
 c->ticket=++c->nonce;c->pending=k;c->inflight=1;
 *out=(VocSample){c->ticket,a->frame,b->frame,a->info.serial,b->info.serial,n,d};
 return leave(c,VOC_SAMPLE);
}
int voc_finish(VocClock*c,uint64_t ticket,int success){
 int e=enter(c);if(e<0)return e;
 if(!c->inflight||!ticket||ticket!=c->ticket||(success!=0&&success!=1))return leave(c,VOC_INVALID);
 if(success){c->last=c->pending;c->has_last=1;}c->inflight=0;c->ticket=0;return leave(c,1);
}
int voc_rate(VocClock*c,uint32_t rate,uint64_t now){
 int e=enter(c);if(e<0)return e;if(c->inflight)return leave(c,VOC_BUSY);
 if(!rate_ok(rate)||now<c->last_now)return leave(c,VOC_INVALID);
 c->hz=rate;c->anchor=c->last_now=now;c->ordinal=0;return leave(c,1);
}
int voc_destroy(VocClock**p){
 if(!p||!*p)return VOC_INVALID;
 VocClock*c=*p;int e=enter(c);if(e<0)return e;
 if(c->inflight)return leave(c,VOC_BUSY);
 drop(c,&c->previous);drop(c,&c->current);free(c);*p=NULL;return 1;
}
