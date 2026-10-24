/* Actual HOST ownership glue. Own integration: djultra64. */
#include "scene_actor_frames.h"
#include "actor_runtime.h"
static int configured,record_actor;
int topspin_scene_actor_init(int enabled){
 if(configured||(enabled!=0&&enabled!=1))return 0;
 int result=topspin_actor_runtime_init(enabled);
 if(result!=(enabled?TAR_OK:TAR_OFF))return 0;
 configured=1;record_actor=enabled;return 1;
}
static int attest(void*u,const VsjFrame*f,const void*aux,uint64_t*domain){
 (void)u;(void)domain;
 if(!topspin_actor_seal_validate(aux,f))return -1;
 /* Actor-only ownership is never whole-scene interpolation authority. */
 return 0;
}
static void release(void*u,void*aux){
 (void)u;TopspinActorSeal*owned=aux;(void)topspin_actor_seal_release(&owned);
}
int topspin_scene_actor_output(VsoOutput*output){
 if(!configured||!output)return 0;
 if(!record_actor)return 1;
 VsoAuxConfig c={NULL,attest,release};return vso_aux_configure(output,&c)==1;
}
int topspin_scene_actor_adopt(VsoOutput*output,VsjFrame**frame,uint64_t sample,uint64_t published,const void*policy,size_t bytes){
 if(!configured||!output||!frame||!*frame)return VOC_INVALID;
 if(!record_actor)return vso_adopt(output,frame,sample,published,policy,bytes);
 TopspinActorSeal*owned=NULL;
 int status=topspin_actor_finalize(*frame,&owned);
 /* All copied bindings are independently owned; no CPU scope is waited on.
  * Retirement obtains its own authentic HOST packet and retains partial scopes. */
 (void)topspin_actor_retire_host();
 if(status!=TAR_OK)return vso_adopt(output,frame,sample,published,policy,bytes);
 void*aux=owned;
 int accepted=vso_enqueue_visual(output,frame,&aux,sample,published,policy,bytes);
 if(accepted!=1){owned=aux;(void)topspin_actor_seal_release(&owned);}
 return accepted;
}
