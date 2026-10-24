/* Regenerable, observational title wrappers. Own work: djultra64.
 * No guest register, clock, memory, x87 or simulation-step changes here. */
#include "timing_trace.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
extern _Thread_local uint32_t g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern uint32_t GetCurrentThreadId(void);
extern void sub_00024A90_gen(void);
extern void sub_00057140_gen(void);
extern void sub_000265A0_gen(void);
extern void sub_00029800_gen(void);
extern void sub_0001C4D0_gen(void);
extern void sub_0002C5F0_gen(void);
static uint32_t word(uint32_t va) {
    uint32_t value=0;
    if(va>=0x10000 && va<=0x03fffffc)
        memcpy(&value,(void*)((uintptr_t)g_xbox_mem_offset+va),4);
    return value;
}
void sub_00024A90(void) {
    uint32_t timer=g_ecx;
    sub_00024A90_gen();
    if(!topspin_timing_trace_enabled() || timer<0x10000 || timer>0x03ffffd8) return;
    TopspinTimingSample sample={0};
    sample.wall_ns=SDL_GetTicksNS();sample.guest_thread=GetCurrentThreadId();
    sample.timer_va=timer;sample.engine_va=word(0x3054ac);sample.interval=word(0x1b939c);
    sample.delta_us=word(timer);sample.dt_bits=word(timer+4);sample.scale_bits=word(timer+0xc);
    sample.timer_count=word(timer+0x10);sample.override_bits=word(timer+0x18);
    sample.timestamp_lo=word(timer+0x20);sample.timestamp_hi=word(timer+0x24);
    topspin_timing_trace_timer_sample(&sample);
}
static void phase(uint32_t pc,int entering,uint32_t context) {
    topspin_timing_trace_cpu_phase(pc,entering,GetCurrentThreadId(),context,SDL_GetTicksNS());
}
/* Explicit definitions are intentional: the generator scans this source. */
void sub_00057140(void) {
    if(!topspin_timing_trace_enabled()){sub_00057140_gen();return;}
    uint32_t context=g_ecx;phase(0x57140,1,context);sub_00057140_gen();phase(0x57140,0,context);
}
void sub_000265A0(void) {
    if(!topspin_timing_trace_enabled()){sub_000265A0_gen();return;}
    uint32_t context=g_ecx;phase(0x265a0,1,context);sub_000265A0_gen();phase(0x265a0,0,context);
}
void sub_00029800(void) {
    if(!topspin_timing_trace_enabled()){sub_00029800_gen();return;}
    uint32_t context=g_ecx;phase(0x29800,1,context);sub_00029800_gen();phase(0x29800,0,context);
}
void sub_0001C4D0(void) {
    if(!topspin_timing_trace_enabled()){sub_0001C4D0_gen();return;}
    uint32_t context=g_ecx;phase(0x1c4d0,1,context);sub_0001C4D0_gen();phase(0x1c4d0,0,context);
}
void sub_0002C5F0(void) {
    if(!topspin_timing_trace_enabled()){sub_0002C5F0_gen();return;}
    uint32_t context=g_ecx;phase(0x2c5f0,1,context);sub_0002C5F0_gen();phase(0x2c5f0,0,context);
}
