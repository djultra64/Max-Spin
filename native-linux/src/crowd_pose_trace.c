/* CPU source-pose forensics only. No guest writes. Own work: djultra64. */
#define RECOMP_GENERATED_CODE
#include "crowd_pose_trace.h"
#include "actor_binding_cpu_guard.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t source,animation,post_animation;unsigned old_flag,pre_flag,new_flag,post_flag;} CrowdPose;
static _Atomic int configured; /* 0 unknown,1 initializing,2 off,3 on */
static _Atomic unsigned lines;
static RECOMP_TLS CrowdPose seen[128];
static RECOMP_TLS unsigned seen_count;
extern void sub_000B7C70_crowd_trace_original(void);

static int valid(uint32_t at,size_t bytes){
 uint64_t end=(uint64_t)at+bytes;
 return bytes&&((at>=0x10000u&&end<=0x04000000ull)||(at>=0x80000000u&&end<=0x84000000ull));
}
static uint32_t word(uint32_t at){
 uint32_t value=0;if(valid(at,4))memcpy(&value,(const void*)((uintptr_t)g_xbox_mem_offset+at),4);return value;
}
static unsigned byte(uint32_t at){
 unsigned char value=0;if(valid(at,1))memcpy(&value,(const void*)((uintptr_t)g_xbox_mem_offset+at),1);return value;
}
static int enabled(void){
 int value=atomic_load_explicit(&configured,memory_order_acquire);
 if(!value){int expected=0;if(atomic_compare_exchange_strong_explicit(&configured,&expected,1,memory_order_acq_rel,memory_order_acquire)){
  const char*e=getenv("TOPSPIN_CROWD_POSE_TRACE");value=e&&!strcmp(e,"1")?3:2;
  atomic_store_explicit(&configured,value,memory_order_release);
 }else value=expected;}
 return value==3;
}
static void record(const CrowdPose*s,uint32_t counter){
 unsigned slot=0;
 while(slot<seen_count&&seen[slot].source!=s->source)slot++;
 if(slot<seen_count){if(!memcmp(&seen[slot],s,sizeof *s))return;}
 else if(seen_count>=128)return;
 else seen_count++;
 seen[slot]=*s;
 unsigned index=atomic_load_explicit(&lines,memory_order_relaxed);
 while(index<TOPSPIN_CROWD_POSE_TRACE_LIMIT){
  if(atomic_compare_exchange_weak_explicit(&lines,&index,index+1,memory_order_relaxed,memory_order_relaxed)){
   fprintf(stderr,"[CROWD-POSE] event=%u author=djultra64 counter=%u source=%08X animation=%08X>%08X source38=%u>%u animation269=%u>%u\n",index,counter,s->source,s->animation,s->post_animation,s->old_flag,s->new_flag,s->pre_flag,s->post_flag);return;
  }
 }
}
void sub_000B7C70(void){
 if(atomic_load_explicit(&configured,memory_order_acquire)==2){sub_000B7C70_crowd_trace_original();return;}
 VabGuestCpu cpu;vab_cpu_save(&cpu);int on=enabled();
 CrowdPose state={0};uint32_t counter=0;int observe=0;
 if(on&&atomic_load_explicit(&lines,memory_order_relaxed)<TOPSPIN_CROWD_POSE_TRACE_LIMIT&&valid(cpu.gpr[1],0x39)){
  state.source=cpu.gpr[1];state.animation=word(state.source+0x20);
  if(valid(state.animation,0x26a)){observe=1;counter=word(0x30dbc8);state.old_flag=byte(state.source+0x38);state.pre_flag=byte(state.animation+0x269);}
 }
 vab_cpu_restore(&cpu);sub_000B7C70_crowd_trace_original();vab_cpu_save(&cpu);
 if(observe){state.post_animation=word(state.source+0x20);state.new_flag=byte(state.source+0x38);state.post_flag=valid(state.post_animation,0x26a)?byte(state.post_animation+0x269):UINT32_MAX;record(&state,counter);}
 vab_cpu_restore(&cpu);
}
