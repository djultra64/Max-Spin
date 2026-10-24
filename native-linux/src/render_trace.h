/* Original update/render boundary observations. Own work: djultra64. */
#ifndef TOPSPIN_RENDER_TRACE_H
#define TOPSPIN_RENDER_TRACE_H
#include <stdint.h>
#define TOPSPIN_RENDER_COMPONENTS 13u
#define TOPSPIN_RENDER_AUX_WORDS 25u
typedef struct TopspinRenderComponent {
    uint32_t address,vtable,valid; /* bit0 object readable, bit1 table readable */
    uint32_t callback[9];         /* raw slots 0 through +0x20, never invoked */
} TopspinRenderComponent;
typedef struct TopspinRenderState {
    uint32_t tick,time_bits,step_bits,interval,guest_swaps,swap_valid;
    uint32_t ball_va,ball_vtable,low_va,low_vtable,valid,ball_serial;
    uint32_t ball_position[3],velocity[3],contact_mask;
    uint32_t aux_valid; /* bit0 expected app, bit1 renderer range, bit2 camera
                        * class, bit3 effects class. Unknown classes stay raw. */
    TopspinRenderComponent component[TOPSPIN_RENDER_COMPONENTS];
    uint32_t aux[TOPSPIN_RENDER_AUX_WORDS];
} TopspinRenderState;
typedef struct TopspinRenderSample {
    uint32_t pc,caller,context,context_vtable,guest_thread;
    uint64_t wall_begin_ns,wall_end_ns,cpu_begin_ns,cpu_end_ns;
    TopspinRenderState before,after;
} TopspinRenderSample;
typedef struct TopspinIntervalEvent {
    uint32_t caller,context,principal,valid,argument[3];
    uint64_t wall_begin_ns,wall_end_ns;
    TopspinRenderState before,after;
} TopspinIntervalEvent;
int topspin_interval_events_enabled(void);
int topspin_interval_events_begin(uint64_t elapsed_ns);
void topspin_interval_events_sample(const TopspinIntervalEvent *sample);
int topspin_render_trace_init(void);
int topspin_render_trace_enabled(void);
int topspin_render_trace_wants(uint64_t elapsed_ns);
/* Pair successful begin with sample even when original crosses window end. */
int topspin_render_trace_begin(uint64_t elapsed_ns);
void topspin_render_trace_sample(const TopspinRenderSample *sample);
/* Host main thread outside guest token. Writes once after window/full/force. */
int topspin_render_trace_poll(uint64_t elapsed_ns,int force);
#endif
