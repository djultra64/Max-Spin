/* Observe original CBaseBall post-update state. Own work: djultra64. */
#include "physics_trace.h"
#include "gpu_preempt_timing.h"
#include <stddef.h>
#include <string.h>
extern _Thread_local uint32_t g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern uint32_t GetCurrentThreadId(void);
extern void __real_sub_000E6D60(void);
static uint32_t word(uint32_t va){uint32_t n;memcpy(&n,(void*)((uintptr_t)g_xbox_mem_offset+va),4);return n;}
struct Dispatch {uint32_t caller,context,vtable,owner,owner_vtable;};
static _Thread_local struct Dispatch dispatch;
static _Thread_local uint32_t last_ball;
uint32_t topspin_physics_last_ball(void){return last_ball;}
extern void __real_sub_0018FDF0(void);
void __wrap_sub_0018FDF0(void)
{
    if(!topspin_physics_trace_enabled()){__real_sub_0018FDF0();return;}
    struct Dispatch previous=dispatch,current={0};
    current.context=g_ecx;
    if(g_esp>=0x10000u && g_esp<=0x03fffffcu)current.caller=word(g_esp);
    if(current.context>=0x10000u && current.context<=0x03ffffe8u){
        current.vtable=word(current.context);current.owner=word(current.context+0x14);
        if(current.owner>=0x10000u && current.owner<=0x03fffffcu)current.owner_vtable=word(current.owner);
    }
    dispatch=current;
    __real_sub_0018FDF0();
    dispatch=previous;
}
void __wrap_sub_000E6D60(void)
{
    uint32_t ball=g_ecx;
    __real_sub_000E6D60();
    last_ball=ball;
    if(!topspin_physics_trace_enabled())return;
    uint64_t now=SDL_GetTicksNS();if(!topspin_physics_trace_wants(now))return;
    TopspinPhysicsSample s={0};
    if(topspin_physics_trace_gpu_preempt_enabled()){
        GpuPreemptTiming timing={0};
        if(xbox_GpuPreemptTimingRead(&timing)&&timing.version==GPU_PREEMPT_TIMING_VERSION){
            s.gpu_hold_valid=1;s.gpu_hold_us=timing.total_us;
            s.gpu_hold_count=timing.holds;s.gpu_hold_flags=timing.flags;
        }
    }
    s.dispatch_caller=dispatch.caller;s.dispatch_context=dispatch.context;s.dispatch_vtable=dispatch.vtable;
    s.dispatch_owner=dispatch.owner;s.dispatch_owner_vtable=dispatch.owner_vtable;
    s.wall_ns=now;s.guest_thread=GetCurrentThreadId();s.ball_va=ball;
    s.interval=word(0x1b939c);s.title_tick=word(0x413284);s.title_time_bits=word(0x413280);s.title_step_bits=word(0x2fda8c);
    if(ball>=0x10000u && ball<=0x03ffff3cu){
        s.valid|=1;s.ball_vtable=word(ball);s.low_va=word(ball+0x14);
        for(unsigned j=0;j<3;j++)s.ball_position[j]=word(ball+8+4*j);
        s.ball_serial=word(ball+0xc0);s.ball_contacts[0]=word(ball+0x30);s.ball_contacts[1]=word(ball+0x38);s.ball_contacts[2]=word(ball+0x48);
        if(s.low_va>=0x10000u && s.low_va<=0x03fffe8cu){
            s.valid|=2;s.low_vtable=word(s.low_va);
            for(unsigned j=0;j<3;j++){s.low_position[j]=word(s.low_va+0xc+4*j);s.low_velocity[j]=word(s.low_va+0x24+4*j);}
            s.dt_bits=word(s.low_va+0xc4);s.dt_next_bits=word(s.low_va+0xc8);s.contact_mask=word(s.low_va+0x170);
            s.contact_active=*(const uint8_t*)((uintptr_t)g_xbox_mem_offset+s.low_va+0x160);
        }
    }
    topspin_physics_trace_sample(&s);
}
