/* Bounded, opt-in timing observations. Own work: djultra64. */
#ifndef TOPSPIN_TIMING_TRACE_H
#define TOPSPIN_TIMING_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Every wall_ns/elapsed_ns uses SDL_GetTicksNS(), without subtracting init time.
 * Raw float words are copied by the CPU adapter; no guest pointers are read here.
 * timer_count is timer+10, NOT a proven physics/integration counter.
 * CSV appends host_thread_slot (process-local host thread identity) and
 * thread_cpu_ns (CLOCK_THREAD_CPUTIME_ID, UINT64_MAX if unavailable). Compare
 * CPU clock deltas only within one slot. No clock calls occur for disabled,
 * out-of-window or rejected rows; no guest clocks or scheduling are changed. */
typedef struct TopspinTimingSample {
    uint64_t wall_ns;
    uint32_t guest_thread, timer_va, engine_va, interval;
    uint32_t delta_us, dt_bits, scale_bits, timer_count, override_bits;
    uint32_t timestamp_lo, timestamp_hi;
} TopspinTimingSample;
/* Call once before starting guest/GPU threads. Absent PATH is a cheap disabled
 * default. Returns 0 on invalid opt-in config, 1 otherwise. */
int topspin_timing_trace_init(void);
int topspin_timing_trace_enabled(void);
void topspin_timing_trace_cpu_phase(uint32_t pc, int entering,
    uint32_t guest_thread, uint32_t context, uint64_t elapsed_ns);
void topspin_timing_trace_timer_sample(const TopspinTimingSample *sample);
/* GPU thread: copies host counters + latest published CPU sample, no guest read.
 * Use actual guest swap/vblank counters; UINT64_MAX explicitly means unknown. */
void topspin_timing_trace_present(uint64_t elapsed_ns, uint64_t host_frame,
    uint64_t guest_swaps, uint64_t vblanks);
/* Host main thread, outside guest_turn: writes ONCE after end/full, or force at
 * shutdown. Poll during the run so watchdog exit does not lose the capture.
 * Returns 0 on write error, 1 otherwise (also while still collecting/disabled). */
int topspin_timing_trace_poll(uint64_t elapsed_ns, int force);
#ifdef __cplusplus
}
#endif
#endif
