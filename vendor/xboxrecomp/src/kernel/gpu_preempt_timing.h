/* Optional current-thread GPU-preempt hold totals. Own work: djultra64. */
#ifndef XBOX_GPU_PREEMPT_TIMING_H
#define XBOX_GPU_PREEMPT_TIMING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GPU_PREEMPT_TIMING_VERSION 1u
enum {GPU_PREEMPT_TIMING_SATURATED=1u,GPU_PREEMPT_TIMING_BAD_DURATION=2u};
typedef struct {uint32_t version,flags;uint64_t total_us,holds;} GpuPreemptTiming;
/* Startup exactly once BEFORE guest workers. DefaultOFF. No environment reads.
 * This does not enable/disable GPU preemption or alter its wait behavior. */
int xbox_GpuPreemptTimingConfigure(int enabled);
/* Calling thread's monotonic totals only. Never GPU-executor/TLS-owner aliases.
 * Returns0OFF/null without touching*out. Returns1 including zero sample totals.
 * No clocks, locks, waits, allocation, IO or guest-memory reads. */
int xbox_GpuPreemptTimingRead(GpuPreemptTiming*out);
#ifdef __cplusplus
}
#endif
#endif
