/* CPU-only observations around original render/update. Own work: djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "render_trace.h"
#include <stddef.h>
#include <string.h>
#include <time.h>
extern _Thread_local uint32_t g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern uint32_t GetCurrentThreadId(void);
extern uint32_t topspin_physics_last_ball(void);
extern void __real_sub_000EB620(void);
extern void __real_sub_000ED720(void);
extern void __real_sub_000EC950(void);
static uint32_t word(uint32_t va){uint32_t n;memcpy(&n,(void*)((uintptr_t)g_xbox_mem_offset+va),4);return n;}
static int range(uint32_t va,uint32_t size){return va>=0x10000u && va<=0x04000000u-size;}
static uint64_t cpu_ns(void){struct timespec t;return clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)?UINT64_MAX:(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static void component(TopspinRenderComponent *out,uint32_t address)
{
    out->address=address;
    if(!range(address,4))return;
    out->valid=1;out->vtable=word(address);
    if(!range(out->vtable,sizeof out->callback))return;
    out->valid|=2;
    for(unsigned j=0;j<9;j++)out->callback[j]=word(out->vtable+4*j);
}
static void auxiliary(TopspinRenderState *s,uint32_t context)
{
    static const uint32_t offsets[12]={0x2d4,0x4988,0x6348,0x499c,
        0x6400,0x6404,0x6408,0x640c,0x6414,0x6418,0x6420,0x63f4};
    /* App-relative fields are interpreted only for this image's observed
     * principal application table. Unknown contexts retain no invented fields. */
    if(!range(context,0x6474)||word(context)!=0x2366fcu)return;
    s->aux_valid=1;s->aux[21]=word(context+0x11c)&255u;
    for(unsigned j=0;j<12;j++){
        uint32_t at=context+offsets[j];
        component(&s->component[j],j<3?at:word(at));
    }
    uint32_t renderer=word(0x30c148);component(&s->component[12],renderer);
    if(range(renderer,0x17c)){
        s->aux_valid|=2;
        s->aux[0]=word(renderer+0x170);s->aux[1]=word(renderer+0x174);
        s->aux[2]=word(renderer+0x178)&0xffffu;
    }
    uint32_t camera=s->component[6].address;
    if(s->component[6].vtable==0x236940u&&range(camera,0x1e8)){
        s->aux_valid|=4;
        s->aux[3]=word(camera+0x24);s->aux[4]=word(camera+0x28);
        s->aux[5]=word(camera+0x30)&0xffffu;
        s->aux[6]=word(camera+0x1e0)&255u;s->aux[7]=word(camera+0x1e4)&255u;
    }
    if(s->aux_valid&4){
        if(range(s->aux[3],4)){s->aux_valid|=32;s->aux[23]=word(s->aux[3]);}
        if(range(s->aux[4],4)){s->aux_valid|=64;s->aux[24]=word(s->aux[4]);}
    }
    uint32_t rules=s->component[8].address;
    if(s->component[8].vtable==0x236c48u&&range(rules,0x11f4)){
        s->aux_valid|=16;s->aux[22]=word(rules+0x11f0);
    }
    uint32_t effects=s->component[9].address;
    if(s->component[9].vtable==0x237c1cu&&range(effects,0xb1c)){
        s->aux_valid|=8;
        for(unsigned j=0;j<7;j++)s->aux[8+j]=word(effects+0xb00+4*j);
        s->aux[17]=word(effects+0xa90);s->aux[18]=word(effects+0xa94);
        s->aux[19]=word(effects+0xa98);s->aux[20]=word(effects+0xaa0);
    }
    s->aux[15]=word(0x2fdb28)&255u;s->aux[16]=word(0x235474);
}
static TopspinRenderState snapshot(uint32_t context)
{
    TopspinRenderState s={0};
    s.tick=word(0x413284);s.time_bits=word(0x413280);s.step_bits=word(0x2fda8c);s.interval=word(0x1b939c);
    uint32_t device=word(0x1b9438);
    if(range(device,0x246cu)){s.guest_swaps=word(device+0x2468);s.swap_valid=1;}
    s.ball_va=topspin_physics_last_ball();
    if(range(s.ball_va,0xc4)){
        s.valid|=1;s.ball_vtable=word(s.ball_va);s.low_va=word(s.ball_va+0x14);s.ball_serial=word(s.ball_va+0xc0);
        for(unsigned i=0;i<3;i++)s.ball_position[i]=word(s.ball_va+8+4*i);
        if(range(s.low_va,0x174)){
            s.valid|=2;s.low_vtable=word(s.low_va);s.contact_mask=word(s.low_va+0x170);
            for(unsigned i=0;i<3;i++)s.velocity[i]=word(s.low_va+0x24+4*i);
        }
    }
    auxiliary(&s,context);
    return s;
}
static void observe(uint32_t pc,void (*original)(void))
{
    if(!topspin_render_trace_enabled()){original();return;}
    uint64_t now=SDL_GetTicksNS();
    if(!topspin_render_trace_begin(now)){original();return;}
    TopspinRenderSample s={0};s.pc=pc;s.context=g_ecx;s.wall_begin_ns=now;s.guest_thread=GetCurrentThreadId();
    if(range(g_esp,4))s.caller=word(g_esp);
    if(range(s.context,4))s.context_vtable=word(s.context);
    s.before=snapshot(s.context);s.cpu_begin_ns=cpu_ns();
    original();
    s.cpu_end_ns=cpu_ns();s.wall_end_ns=SDL_GetTicksNS();s.after=snapshot(s.context);
    topspin_render_trace_sample(&s);
}
void __wrap_sub_000EB620(void){observe(0xeb620,__real_sub_000EB620);}
void __wrap_sub_000ED720(void){observe(0xed720,__real_sub_000ED720);}

/* Exact original secondary-interface event dispatch. Observation only. */
void __wrap_sub_000EC950(void)
{
    if(!topspin_interval_events_enabled()){__real_sub_000EC950();return;}
    uint64_t now=SDL_GetTicksNS();
    if(!topspin_interval_events_begin(now)){__real_sub_000EC950();return;}
    TopspinIntervalEvent s={0};s.context=g_ecx;s.wall_begin_ns=now;
    if(range(g_esp,16)){
        s.valid|=1;s.caller=word(g_esp);
        for(unsigned j=0;j<3;j++)s.argument[j]=word(g_esp+4+j*4);
    }
    if(s.context>=0x110u&&range(s.context-0x110u,0x6474)
       &&word(s.context)==0x2366f8u&&word(s.context-0x110u)==0x2366fcu){
        s.valid|=2;s.principal=s.context-0x110u;
    }
    int relevant=(s.valid&1)&&((s.argument[0]>=0xb0000u&&s.argument[0]<=0xb0008u)||s.argument[0]==0x80001u);
    if(relevant&&(s.valid&2))s.before=snapshot(s.principal);
    __real_sub_000EC950();
    s.wall_end_ns=SDL_GetTicksNS();
    if(relevant&&(s.valid&2))s.after=snapshot(s.principal);
    /* Nonmatching/unchanged calls still complete the pending reservation. */
    if(!relevant)s.valid=0;
    topspin_interval_events_sample(&s);
}
