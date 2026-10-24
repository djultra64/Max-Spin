/* Bounded original-UI recovery; no Career success synthesis. djultra64. */
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "playercreator_save_failure.h"
#include "material_save_operation.h"
#include "actor_binding_cpu_guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void sub_00060350(void);
extern void sub_00092BA0(void);
static _Thread_local int recovering;
static void*ram(uint32_t at,size_t bytes){
 return at>=0x10000u&&(uint64_t)at+bytes<=0x4000000u?(void*)XBOX_PTR(at):NULL;
}
static uint32_t word(uint32_t at){uint32_t v=0;void*p=ram(at,4);if(p)memcpy(&v,p,4);return v;}
static int name_is(uint32_t scene,const char*name){
 uint32_t text=word(scene+0x18c);size_t bytes=strlen(name);
 return text>=0x1000cu&&word(text-12)==bytes&&ram(text,bytes+1)&&!memcmp(ram(text,bytes+1),name,bytes+1);
}
static int guards(unsigned kind,uint32_t owner,uint32_t*system,uint32_t*manager,uint32_t*hub){
 size_t extent=kind==MC_SAVE_OPERATION_CREATOR?0x224:kind==MC_SAVE_OPERATION_SHOP?0x37c:0;
 if(!extent||(owner&3u)||!ram(owner,extent))return 0;
 if(word(owner)!=(kind==MC_SAVE_OPERATION_CREATOR?0x1e3d98u:0x1f6508u))return 0;
 if(kind==MC_SAVE_OPERATION_CREATOR&&!name_is(owner,"pc_saveplayer"))return 0;
 if(*(unsigned char*)ram(owner+0x19c,1)!=1)return 0;
 *system=word(0x2b7524);*manager=word(0x2b7e9c);
 /* Original1B0D0 uses this+1244 as the current scene;92BA0 obtains it from
  * global2B7524. Context+0C is the cached hub, not current-scene authority. */
 if(!ram(*system,0x1248)||word(*system+0x1244)!=owner||
    !ram(*manager,0x4e2)||word(*manager)!=0x1e8f80u)return 0;
 uint32_t context=word(0x2b7768),creator=word(0x390a5c);
 if(!ram(context,0x24)||word(context)!=0x1e4210u||!ram(creator,0x3c0)||word(context+8)!=creator)return 0;
 *hub=word(context+0xc);
 return ram(*hub,0x209)&&name_is(*hub,"pc_hub");
}
static int invoke(void(*fn)(void),uint32_t object,uint32_t stack,unsigned count,const uint32_t*args){
 uint32_t*words=ram(stack,4+count*4);if(!words)return 0;
 words[0]=0;for(unsigned i=0;i<count;i++)words[i+1]=args[i];
 g_ecx=object;g_esp=stack;fn();return g_esp==stack+4+count*4;
}
void topspin_creator_save_failure_recover(unsigned kind,uint32_t owner){
 if(recovering)return;
 VabGuestCpu cpu;vab_cpu_save(&cpu);
 const char*flag=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");uint32_t system=0,manager=0,hub=0;
 if(!flag||strcmp(flag,"1"))goto done;
 if(mc_save_operation_active()||!guards(kind,owner,&system,&manager,&hub)||g_esp<0x14000u||g_esp>0x4000000u){
  fprintf(stderr,"[CREATOR-SAVE-FAILURE] rejected UI recovery kind=%u owner=%08x; scene/context/stack authority missing\n",kind,owner);goto done;
 }
 uint32_t base=g_esp-0x4000,stack=base+0x1800;unsigned char borrowed[0x4000];
 memcpy(borrowed,ram(base,sizeof borrowed),sizeof borrowed);recovering=1;
 int ok=1;
 /*60350 is the original SaveScene PrevScene cleanup, never a Shop method. */
 if(kind==MC_SAVE_OPERATION_CREATOR)ok=invoke(sub_00060350,owner,stack,0,NULL);
 uint32_t after_system=0,after_manager=0,after_hub=0;
 ok=ok&&guards(kind,owner,&after_system,&after_manager,&after_hub)&&system==after_system&&manager==after_manager&&hub==after_hub;
 if(ok){
  /* "$Init" only restores HubOptionList focus. "$InitPlayerCreator" is a
   * different original action and must not be used to recover a rejected Save. */
  const uint32_t args[]={0x1e3100,0x1e2d3c,1};
  ok=invoke(sub_00092BA0,manager,stack,3,args);
 }
 int activated=ok&&word(system+0x1244)==hub&&ram(hub,0x19d)&&*(unsigned char*)ram(hub+0x19c,1)==1;
 fprintf(stderr,"[CREATOR-SAVE-FAILURE] save rejected; original hub recovery kind=%u cleanup=%d transition_abi=%d hub_active=%d; no Save success\n",kind,kind==MC_SAVE_OPERATION_CREATOR,ok,activated);
 memcpy(ram(base,sizeof borrowed),borrowed,sizeof borrowed);recovering=0;
done:
 vab_cpu_restore(&cpu);
}
