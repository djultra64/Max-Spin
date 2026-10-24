/* Observe generated originals exactly once; no pacing/state changes. djultra64. */
#include "replay_sample_capture.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fenv.h>
extern _Thread_local uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
extern _Thread_local double g_fp_stack[8];
extern _Thread_local int g_fp_top,g_fp_cmp,g_df;
extern _Thread_local uint16_t g_fp_control_word,g_fp_cc;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern void sub_000FFBC0_gen(void);
extern void sub_000FF0B0_gen(void);
static _Thread_local int observing;
static void snapshot_cpu(void)
{
  TopspinReplayCpuState state={0};
  const uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,(uint32_t)g_fp_top};
  memcpy(state.gpr,regs,sizeof regs);memcpy(state.fp_stack,g_fp_stack,sizeof state.fp_stack);
  state.fp_control=g_fp_control_word;state.fp_cc=g_fp_cc;state.fp_cmp=g_fp_cmp;state.host_round=fegetround();state.direction_flag=g_df;
  topspin_replay_sample_capture_cpu(&state);
}
void sub_000FFBC0(void)
{
 int selected=0;
 if(!observing&&topspin_replay_sample_capture_enabled()) {
  uint64_t now=SDL_GetTicksNS();
  if(topspin_replay_sample_capture_wants(now))
   selected=topspin_replay_sample_capture_enter(now,g_ecx,g_esp,
       (const void*)(uintptr_t)g_xbox_mem_offset,0x04000000u);
 }
 int previous=observing;
 if(selected){
  snapshot_cpu();observing=1;
 }
 sub_000FFBC0_gen();
 if(selected)topspin_replay_sample_capture_leave();
 observing=previous;
}
void sub_000FF0B0(void)
{
 int selected=0;
 if(!observing&&topspin_replay_sample_capture_enabled()){
  uint64_t now=SDL_GetTicksNS();
  if(topspin_replay_sample_capture_wants(now))
   selected=topspin_replay_sample_capture_enter_apply(now,g_ecx,g_esp,
       (const void*)(uintptr_t)g_xbox_mem_offset,0x04000000u);
 }
 if(selected){snapshot_cpu();observing=1;}
 int capture=observing;
 if(capture)topspin_replay_sample_capture_apply_before(g_ecx,g_esp);
 sub_000FF0B0_gen();
 if(capture)topspin_replay_sample_capture_apply_after();
 if(selected){topspin_replay_sample_capture_leave();observing=0;}
}
