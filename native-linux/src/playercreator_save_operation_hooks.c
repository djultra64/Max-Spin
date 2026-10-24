/* Whole-operation wrappers; original guest bodies execute once. djultra64. */
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "playercreator_save_operation_hooks.h"
#include "material_save_operation.h"
#include "actor_binding_cpu_guard.h"
#include "playercreator_model_checkpoint.h"
#include <string.h>
extern void sub_000626A0_gen(void);
extern void sub_000FD550_gen(void);
/* Required root session reset: recoverable operation latches only, never RJP. */
extern void mc_save_session_operation_begin(void);

int topspin_creator_save_checkpoint(void){
 VabGuestCpu cpu;vab_cpu_save(&cpu);
 int complete=mc_save_operation_finish();
 vab_cpu_restore(&cpu);return complete;
}
int topspin_creator_save_continue(void){
 VabGuestCpu cpu;vab_cpu_save(&cpu);
 int complete=mc_save_operation_can_continue();
 vab_cpu_restore(&cpu);return complete;
}
static void run(unsigned kind,void(*original)(void)){
 VabGuestCpu cpu;vab_cpu_save(&cpu);
 uint32_t owner=g_ecx,caller=0;
 if(g_esp>=0x10000u&&(uint64_t)g_esp+4<=0x4000000u)
  memcpy(&caller,(void*)XBOX_PTR(g_esp),4);
 int entered=mc_save_operation_enter(kind,owner,caller);
 int checkpoint=entered&&mc_save_operation_active()&&kind==MC_SAVE_OPERATION_CREATOR;
 if(entered&&mc_save_operation_active())mc_save_session_operation_begin();
 vab_cpu_restore(&cpu);
 if(checkpoint){
  int captured=topspin_creator_model_checkpoint_begin(kind,owner);
  vab_cpu_restore(&cpu);
  if(!captured){
   /* Refuse before C6A30 can alter any component. The original Creator
    * callback is void, ret4; this failure consumes its argument and return
    * address without manufacturing a Save-success value. */
   mc_save_operation_reject();(void)mc_save_operation_finish();mc_save_operation_leave();
   vab_cpu_restore(&cpu);
   topspin_creator_save_failure_recover(kind,owner);
   topspin_creator_model_checkpoint_discard();
   vab_cpu_restore(&cpu);g_esp+=8;return;
  }
 }
 original();
 /* A failed nested enter does not own/clear the outer transaction. Its real
  * body still executes; the failed outer scope prevents any successful handoff. */
 if(!entered)return;
 vab_cpu_save(&cpu);
 int complete=mc_save_operation_finish();
 mc_save_operation_leave();
 vab_cpu_restore(&cpu);
 if(!complete){
  if(checkpoint)(void)topspin_creator_model_checkpoint_restore(kind,owner);
  vab_cpu_restore(&cpu);
  topspin_creator_save_failure_recover(kind,owner);
  vab_cpu_restore(&cpu);
  if(checkpoint)topspin_creator_model_checkpoint_observe("after-ui-recovery");
 }
 if(checkpoint)topspin_creator_model_checkpoint_discard();
 vab_cpu_restore(&cpu);
}
void sub_000626A0(void){run(MC_SAVE_OPERATION_CREATOR,sub_000626A0_gen);}
void sub_000FD550(void){run(MC_SAVE_OPERATION_SHOP,sub_000FD550_gen);}
