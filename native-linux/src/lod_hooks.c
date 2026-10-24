/* Original Game selector observation; own work: djultra64.
 * Manual scanner requires this public name and separate canonical _gen extern. */
#include "lod_trace.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
extern _Thread_local uint32_t g_esp,g_eax;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern uint32_t GetCurrentThreadId(void);
extern void sub_00086810_gen(void);
static int range(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static uint32_t word(uint32_t a){uint32_t v;memcpy(&v,(void*)((uintptr_t)g_xbox_mem_offset+a),4);return v;}
void sub_00086810(void){
 TopspinLodSample s;int eligible=0;
 if(topspin_lod_trace_enabled()&&range(g_esp,16)){
  memset(&s,0,sizeof s);
  s.caller=word(g_esp);uint32_t pos=word(g_esp+4);s.thresholds=word(g_esp+8);s.count=word(g_esp+12);
  if(s.caller==0x79afbu&&s.count&&s.count<=TOPSPIN_LOD_TIERS&&pos>=0x10010u){
   s.owner=pos-0x10u;
   if(range(s.owner,0xb8)&&range(s.thresholds,s.count*4u)&&word(s.owner+0xb0)==s.thresholds&&word(s.owner+0xb4)==s.count){
    s.meshes=word(s.owner+0xa0);
    if(range(s.meshes,s.count*4u)){
     s.forced=word(0x2fc59c);eligible=1;
     for(unsigned j=0;j<3;j++){s.position[j]=word(pos+j*4u);if((s.position[j]&0x7f800000u)==0x7f800000u)eligible=0;}
     for(unsigned j=0;j<s.count;j++){
      s.mesh[j]=word(s.meshes+j*4u);s.threshold[j]=word(s.thresholds+j*4u);
      if(!range(s.mesh[j],4)||(s.threshold[j]&0x7f800000u)==0x7f800000u)eligible=0;
     }
    }
   }
  }
 }
 sub_00086810_gen();
 /* Post-original reads only the saved CPU return and private snapshot. */
 if(eligible){s.selected=g_eax;if(s.selected<s.count){s.wall_ns=SDL_GetTicksNS();s.thread=GetCurrentThreadId();topspin_lod_trace_sample(&s);}}
}
