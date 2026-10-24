/* Bounded original ball-state observations. Own work: djultra64. */
#ifndef TOPSPIN_PHYSICS_TRACE_H
#define TOPSPIN_PHYSICS_TRACE_H
#include <stdint.h>
typedef struct TopspinPhysicsSample {
    uint64_t wall_ns;
    uint32_t guest_thread,ball_va,ball_vtable,low_va,low_vtable,valid;
    uint32_t ball_position[3],ball_serial,ball_contacts[3];
    uint32_t low_position[3],low_velocity[3],dt_bits,dt_next_bits;
    uint32_t contact_active,contact_mask,interval;
    uint32_t title_tick,title_time_bits,title_step_bits;
    uint32_t dispatch_caller,dispatch_context,dispatch_vtable,dispatch_owner,dispatch_owner_vtable;
    /* Same guest thread, cumulative observed host holds; never physics time. */
    uint64_t gpu_hold_us,gpu_hold_count;
    uint32_t gpu_hold_valid,gpu_hold_flags;
} TopspinPhysicsSample;
/* Initialize before guest threads. Exact opt-in PATH; strict integer seconds
 * START_SECONDS(default14), END_SECONDS(default40), absolute SDL elapsed time. */
int topspin_physics_trace_init(void);
int topspin_physics_trace_enabled(void);
int topspin_physics_trace_wants(uint64_t elapsed_ns);
int topspin_physics_trace_gpu_preempt_enabled(void);
void topspin_physics_trace_sample(const TopspinPhysicsSample *sample);
/* Main thread outside guest token: write once after end/full or force.
 * Producers never perform file IO or wait for this writer. */
int topspin_physics_trace_poll(uint64_t elapsed_ns,int force);
#endif
