/* Full represented guest CPU/fenv preserved around observations. djultra64. */
#include "actor_draw_hooks.h"
#include "visual_pb_hooks.h"
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "actor_binding_cpu_guard.h"
#include <string.h>
static VadbCollector*collector;
int vadb_hooks_install(VadbCollector*c){if(collector||!c||!c->initialized||!c->config.enabled)return 0;collector=c;return 1;}
static int read_at(uint32_t at,uint32_t*out){return at<=UINT32_MAX-4&&collector->config.reader.read(collector->config.reader.user,at,out,4);}
static void call_original(unsigned va,void(*original)(void)){
 if(!collector){original();return;}
 VabGuestCpu before;vab_cpu_save(&before);uint32_t object=before.gpr[1],caller=0;uint64_t ticket=0;unsigned entered=0;
 int scoped=topspin_pb_observe_scope_enter();
 if(scoped){
  if(before.gpr[3]<=UINT32_MAX-16)read_at(before.gpr[3],&caller);
  switch(va){
   case 0x862b0:ticket=vadb_mesh_ctor_begin(collector,object);entered=1;break;
   case 0x86430:vadb_mesh_dtor(collector,object);break;
   case 0xf1a30:vadb_owner_begin(collector,object);entered=1;break;
   case 0x146320:if(collector->scope==1){vadb_actor_begin(collector,object,caller);entered=1;}break;
   case 0x857c0:if(collector->scope==2){vadb_model_begin(collector,object);entered=1;}break;
   case 0x86a00:if(collector->scope==3){vadb_mesh_begin(collector,object);entered=1;}break;
   case 0x74520:if(collector->scope==4){uint32_t palette=0,indices=0,count=0,renderer=0;int ok=before.gpr[3]<=UINT32_MAX-16&&read_at(before.gpr[3]+4,&palette)&&read_at(before.gpr[3]+8,&indices)&&read_at(before.gpr[3]+12,&count)&&read_at(0x30c148,&renderer);if(ok)vadb_upload_begin(collector,caller,renderer,palette,indices,count);entered=1;}break;
  }
  topspin_pb_observe_scope_leave();
 }
 vab_cpu_restore(&before);original();VabGuestCpu after;vab_cpu_save(&after);scoped=topspin_pb_observe_scope_enter();
 if(scoped){switch(va){
  case 0x862b0:if(entered)vadb_mesh_ctor_end(collector,object,ticket,after.gpr[0]);break;
  case 0x74b90:if(collector->scope==1&&caller==0xf1a6c)vadb_owner_count(collector,caller,after.gpr[0]);break;
  case 0xf1a30:if(entered)vadb_owner_end(collector);break;
  case 0x146320:if(entered)vadb_actor_end(collector);break;
  case 0x857c0:if(entered)vadb_model_end(collector);break;
  case 0x86a00:if(entered)vadb_mesh_end(collector);break;
  case 0x74520:if(entered)vadb_upload_end(collector);break;
 }topspin_pb_observe_scope_leave();}vab_cpu_restore(&after);
}
#define WRAP(name,address) extern void name##_vadb_original(void);void name(void){call_original(address,name##_vadb_original);}
WRAP(sub_000862B0,0x862b0)
WRAP(sub_00086430,0x86430)
WRAP(sub_000F1A30,0xf1a30)
WRAP(sub_00074B90,0x74b90)
WRAP(sub_00146320,0x146320)
WRAP(sub_000857C0,0x857c0)
WRAP(sub_00086A00,0x86a00)
WRAP(sub_00074520,0x74520)
