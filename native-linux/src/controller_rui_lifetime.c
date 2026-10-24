/* Own immutable prompt-resource lifetime: djultra64. */
#include "controller_rui_lifetime.h"
#include <stdlib.h>
#include <string.h>
typedef struct {uint32_t object,original;unsigned generation;} Resource;
typedef struct {uint32_t object,original,header;unsigned generation;uint8_t labels[4];uintptr_t token;size_t bytes;int retired;} Variant;
struct TopSpinRuiLifetime {TopSpinRuiLifetimeOps ops;size_t bytes,budget;unsigned generation;Resource resource[TOPSPIN_RUI_LIVE_RESOURCES];Variant variant[TOPSPIN_RUI_LIVE_VARIANTS];};
static int labels_valid(const uint8_t*l){if(!l)return 0;for(unsigned i=0;i<4;i++)if(l[i]<1||l[i]>8)return 0;return 1;}
static Resource*resource(TopSpinRuiLifetime*s,uint32_t object){for(unsigned i=0;i<TOPSPIN_RUI_LIVE_RESOURCES;i++)if(s->resource[i].object==object)return&s->resource[i];return NULL;}
TopSpinRuiLifetime*topspin_rui_lifetime_create(const TopSpinRuiLifetimeOps*ops,size_t budget){
 if(!ops||!ops->upload||!ops->quiesce||!ops->release||!budget)return NULL;
 TopSpinRuiLifetime*s=calloc(1,sizeof*s);if(s){s->ops=*ops;s->budget=budget;}return s;
}
void topspin_rui_lifetime_retire(TopSpinRuiLifetime*s,uint32_t object){
 if(!s||!object)return;
 Resource*r=resource(s,object);
 if(!r)return;
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++){Variant*v=&s->variant[i];if(v->object==object&&v->generation==r->generation)v->retired=1;}
 memset(r,0,sizeof*r);
}
int topspin_rui_lifetime_attach(TopSpinRuiLifetime*s,uint32_t object,uint32_t original){
 if(!s||!object||!original||s->generation==~0u)return 0;
 Resource*r=resource(s,object);
 if(r&&r->original==original)return 1;
 /* Check replacement before retiring the old mapping. A rejected attach
  * cannot alter a still-live resource or its consumer variants. */
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_RESOURCES;i++)
  if(s->resource[i].object&&s->resource[i].object!=object&&s->resource[i].original==original)return 0;
 if(r)topspin_rui_lifetime_retire(s,object);
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_RESOURCES;i++)if(!s->resource[i].object){s->resource[i]=(Resource){object,original,++s->generation};return 1;}
 return 0;
}
int topspin_rui_lifetime_publish(TopSpinRuiLifetime*s,uint32_t object,const uint8_t labels[4],const void*xpr,size_t bytes){
 if(!s||!labels_valid(labels)||!xpr||!bytes)return 0;
 Resource*r=resource(s,object);
 if(!r)return 0;
 Variant*empty=NULL;
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++){Variant*v=&s->variant[i];if(!v->object){if(!empty)empty=v;continue;}if(!v->retired&&v->object==object&&v->generation==r->generation&&!memcmp(v->labels,labels,4))return 1;}
 if(!empty||bytes>s->budget-s->bytes)return 0;
 uint32_t header=0;uintptr_t token=0;if(!s->ops.upload(s->ops.user,xpr,bytes,&header,&token))return 0;
 /* Uploader must reserve distinct addresses; failed validation still needs
  * a quiescent retirement instead of an unsafe immediate release. */
 *empty=(Variant){object,r->original,header,r->generation,{0},token,bytes,0};memcpy(empty->labels,labels,4);s->bytes+=bytes;
 if(!header||header==r->original){empty->retired=1;return 0;}
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++)if(&s->variant[i]!=empty&&s->variant[i].object&&s->variant[i].header==header){empty->retired=1;return 0;}
 return 1;
}
uint32_t topspin_rui_lifetime_bind(const TopSpinRuiLifetime*s,uint32_t original,const uint8_t labels[4]){
 if(!s||!labels_valid(labels))return original;
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++){const Variant*v=&s->variant[i];if(v->object&&!v->retired&&v->original==original&&!memcmp(v->labels,labels,4))return v->header;}
 return original;
}
int topspin_rui_lifetime_collect(TopSpinRuiLifetime*s){
 if(!s)return 0;
 uint32_t headers[TOPSPIN_RUI_LIVE_VARIANTS];size_t count=0;
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++)if(s->variant[i].object&&s->variant[i].retired)headers[count++]=s->variant[i].header;
 if(!count)return 1;
 if(!s->ops.quiesce(s->ops.user,headers,count))return 0;
 for(unsigned i=0;i<TOPSPIN_RUI_LIVE_VARIANTS;i++){Variant*v=&s->variant[i];if(v->object&&v->retired){s->ops.release(s->ops.user,v->token);s->bytes-=v->bytes;memset(v,0,sizeof*v);}}
 return 1;
}
int topspin_rui_lifetime_destroy(TopSpinRuiLifetime*s){if(!s)return 1;for(unsigned i=0;i<TOPSPIN_RUI_LIVE_RESOURCES;i++)if(s->resource[i].object)topspin_rui_lifetime_retire(s,s->resource[i].object);if(!topspin_rui_lifetime_collect(s))return 0;free(s);return 1;}
size_t topspin_rui_lifetime_bytes(const TopSpinRuiLifetime*s){return s?s->bytes:0;}
