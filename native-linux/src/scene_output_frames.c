/* Executor-owner lifetime adapter. Own work: djultra64. */
#include "scene_output_frames.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
 VsjFrame*frame;void*aux;VocFrameInfo info;uint32_t references;
 size_t policy_bytes;unsigned char policy[VSO_POLICY_BYTES];
} Frame;
struct VsoOutput {
 VsoConfig config;VsoAuxConfig auxiliary;int aux_configured,started;
 VocClock*clock;Frame slots[3],*previous,*current,*flight_previous,*flight_current,*pending;
 uint64_t ticket;int busy,revoked;
};
static int enter(VsoOutput*o){if(!o||o->busy)return 0;o->busy=1;if(!o->config.owner(o->config.user)){o->busy=0;return 0;}return 1;}
static int leave(VsoOutput*o,int result){o->busy=0;return result;}
static int owned(void*u){VsoOutput*o=u;return o->config.owner(o->config.user);}
static Frame*slot(VsoOutput*o,const void*p){for(unsigned i=0;i<3;i++)if(p==&o->slots[i]&&o->slots[i].frame&&o->slots[i].references)return&o->slots[i];return NULL;}
static int classify(VsoOutput*o,const VsjFrame*f,const void*aux,uint64_t*domain,uint32_t*flags){
 *domain=1;*flags=VOC_COMPLETE;
 if(o->aux_configured){
  if(aux){uint64_t d=0;int result=o->auxiliary.attest(o->auxiliary.user,f,aux,&d);if(result<0)return 0;if(result){if(!d)return 0;*domain=d;*flags|=VOC_INTERPOLABLE;}}
 }else if(o->config.attest){uint64_t d=0;int result=o->config.attest(o->config.user,f,&d);if(result<0)return 0;if(result){if(!d)return 0;*domain=d;*flags|=VOC_INTERPOLABLE;}}
 return 1;
}
static void drop(VsoOutput*o,Frame*f){if(f&&!--f->references){
 VsjFrame*j=f->frame;void*aux=f->aux;
 /* Last lease only: finish's actual consumer fence is the caller boundary. */
 if(aux)o->auxiliary.release(o->auxiliary.user,aux);
 (void)vsj_release(&j);memset(f,0,sizeof*f);
}}
static int retain(void*u,const void*p,void**out){
 VsoOutput*o=u;Frame*f=slot(o,p);*out=NULL;
 if(!owned(o)||!f||f->references==UINT32_MAX)return 0;
 f->references++;*out=f;return 1;
}
static int inspect(void*u,const void*p,void*lease,VocFrameInfo*out){
 VsoOutput*o=u;Frame*f=slot(o,p);uint64_t domain;uint32_t flags;
 if(!owned(o)||!f||lease!=f||!out||!vsj_validate(f->frame)||!classify(o,f->frame,f->aux,&domain,&flags)||domain!=f->info.domain||flags!=f->info.flags)return 0;
 *out=f->info;return 1;
}
static void release(void*u,const void*p,void*lease){VsoOutput*o=u;Frame*f=slot(o,p);if(owned(o)&&f&&lease==f)drop(o,f);}
static int start(VsoOutput*o,uint64_t now){VocProvider p={o,owned,retain,inspect,release};return voc_create(&p,o->config.rate,now,&o->clock)==1;}
static int clear(VsoOutput*o){
 if(o->clock&&voc_destroy(&o->clock)!=1)return 0;
 drop(o,o->previous);drop(o,o->current);drop(o,o->pending);o->previous=o->current=o->pending=NULL;o->revoked=0;return 1;
}
VsoOutput*vso_create(const VsoConfig*c){
 if(!c||!c->owner||!c->owner(c->user)||(c->rate&&c->rate!=60&&c->rate!=90&&c->rate!=120))return NULL;
 VsoOutput*o=calloc(1,sizeof*o);if(o)o->config=*c;return o;
}
int vso_aux_configure(VsoOutput*o,const VsoAuxConfig*c){
 if(!enter(o))return VOC_OWNER;
 if(!c||!c->attest||!c->release||o->aux_configured||o->started||o->clock)return leave(o,VOC_INVALID);
 o->auxiliary=*c;o->aux_configured=1;return leave(o,1);
}
static int stage(VsoOutput*o,VsjFrame*frame,void*aux,uint64_t sample,uint64_t published,const void*policy,size_t bytes,Frame**out){
 if(!frame||aux==(void*)frame||!policy||!bytes||bytes>VSO_POLICY_BYTES||!vsj_validate(frame))return VOC_INVALID;
 Frame*f=NULL;
 for(unsigned i=0;i<3;i++){
  if(o->slots[i].frame==frame||o->slots[i].aux==(void*)frame||(aux&&(o->slots[i].aux==aux||(void*)o->slots[i].frame==aux)))return VOC_INVALID;
  if(!o->slots[i].references)f=&o->slots[i];
 }
 if(!f)return VOC_BUSY;
 const VsjStamp*s=vsj_stamp(frame);uint64_t domain;uint32_t flags;
 if(!s||!classify(o,frame,aux,&domain,&flags))return VOC_INVALID;
 f->frame=frame;f->aux=aux;f->references=1;
 f->info=(VocFrameInfo){s->device_epoch,s->continuity.scene,s->continuity.view,domain,s->frame_serial,s->simulation_tick,sample,published,flags};
 f->policy_bytes=bytes;memcpy(f->policy,policy,bytes);*out=f;return 1;
}
static int offer(VsoOutput*o,Frame*f){
 int result=voc_offer(o->clock,f);
 if(result!=1)return result<0?result:VOC_INVALID;
 drop(o,o->previous);o->previous=o->current;o->current=f;return 1;
}
static int adopt(VsoOutput*o,VsjFrame**frame,void**aux,uint64_t sample,uint64_t published,const void*policy,size_t bytes,int queued,int visual){
 if(!enter(o))return VOC_OWNER;
 o->started=1;
 if(o->revoked||o->pending||(o->ticket&&!queued))return leave(o,VOC_BUSY);
 if(!frame||!*frame||(visual&&(!o->aux_configured||!aux||!*aux||(void*)frame==(void*)aux)))return leave(o,VOC_INVALID);
 Frame*f=NULL;int result=stage(o,*frame,visual?*aux:NULL,sample,published,policy,bytes,&f);
 if(result!=1)return leave(o,result);
 if(o->ticket){o->pending=f;*frame=NULL;*aux=NULL;return leave(o,1);}
 if(!o->clock&&!start(o,published)){memset(f,0,sizeof*f);return leave(o,VOC_INVALID);}
 result=offer(o,f);
 if(result!=1){/* Neither pointer is consumed on a failed immediate offer. */
  memset(f,0,sizeof*f);return leave(o,result);
 }
 *frame=NULL;if(visual)*aux=NULL;return leave(o,1);
}
int vso_adopt(VsoOutput*o,VsjFrame**frame,uint64_t sample,uint64_t published,const void*policy,size_t bytes){return adopt(o,frame,NULL,sample,published,policy,bytes,0,0);}
int vso_adopt_visual(VsoOutput*o,VsjFrame**frame,void**aux,uint64_t sample,uint64_t published,const void*policy,size_t bytes){return adopt(o,frame,aux,sample,published,policy,bytes,0,1);}
int vso_enqueue_visual(VsoOutput*o,VsjFrame**frame,void**aux,uint64_t sample,uint64_t published,const void*policy,size_t bytes){return adopt(o,frame,aux,sample,published,policy,bytes,1,1);}
int vso_poll(VsoOutput*o,uint64_t now,VsoSample*out,uint64_t*wake){
 if(!enter(o))return VOC_OWNER;
 if(!out||!wake)return leave(o,VOC_INVALID);
 memset(out,0,sizeof*out);*wake=UINT64_MAX;
 if(o->revoked||!o->clock)return leave(o,VOC_WAIT);
 VocSample s;int result=voc_poll(o->clock,now,&s,wake);
 if(result==VOC_SAMPLE){
  Frame*a=slot(o,s.previous),*b=slot(o,s.current);
  if(!a||!b){(void)voc_finish(o->clock,s.ticket,0);return leave(o,VOC_STALE);}
  o->ticket=s.ticket;o->flight_previous=a;o->flight_current=b;
  *out=(VsoSample){s.ticket,s.alpha_num,s.alpha_den,a->frame,b->frame,b->policy,b->policy_bytes,a->aux,b->aux};
 }
 return leave(o,result);
}
int vso_ticket_valid(VsoOutput*o,uint64_t ticket){
 if(!enter(o))return 0;
 VocFrameInfo info;
 int ok=ticket&&ticket==o->ticket&&!o->revoked&&o->flight_current&&o->flight_previous&&
  inspect(o,o->flight_current,o->flight_current,&info)&&inspect(o,o->flight_previous,o->flight_previous,&info);
 return leave(o,ok);
}
int vso_finish(VsoOutput*o,uint64_t ticket,int displayed){
 if(!enter(o))return VOC_OWNER;
 if(!o->clock||!ticket||ticket!=o->ticket||(displayed!=0&&displayed!=1))return leave(o,VOC_INVALID);
 int result=voc_finish(o->clock,ticket,o->revoked?0:displayed);
 if(result==1){
  o->ticket=0;o->flight_previous=o->flight_current=NULL;
  if(o->revoked){if(!clear(o))return leave(o,VOC_BUSY);}
  else if(o->pending){
   Frame*f=o->pending;o->pending=NULL;
   /* This is the first clock offer of C, after the original flight finished. */
   if(offer(o,f)!=1){drop(o,f);if(!clear(o))return leave(o,VOC_BUSY);}
  }
 }
 return leave(o,result);
}
int vso_rate(VsoOutput*o,uint32_t rate,uint64_t now){
 if(!enter(o))return VOC_OWNER;
 if(o->ticket)return leave(o,VOC_BUSY);
 if(rate&&rate!=60&&rate!=90&&rate!=120)return leave(o,VOC_INVALID);
 if(o->clock){int result=voc_rate(o->clock,rate,now);if(result!=1)return leave(o,result);}
 o->config.rate=rate;return leave(o,1);
}
int vso_invalidate(VsoOutput*o){
 if(!enter(o))return VOC_OWNER;
 o->revoked=1;if(o->ticket)return leave(o,VOC_BUSY); /* Preserve pending and flight through actual fence. */
 return leave(o,clear(o)?1:VOC_BUSY);
}
int vso_destroy(VsoOutput**ptr){
 if(!ptr||!*ptr)return VOC_INVALID;
 VsoOutput*o=*ptr;if(!enter(o))return VOC_OWNER;
 if(o->ticket||!clear(o))return leave(o,VOC_BUSY);
 free(o);*ptr=NULL;return 1;
}
