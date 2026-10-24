/* Observed normal driver API durations; never completion authority. djultra64. */
#ifndef XBOX_DRIVER_PRESENT_TIMING_H
#define XBOX_DRIVER_PRESENT_TIMING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DPT_VERSION 1u
#define DPT_ACQUIRE_ATTEMPTED 1u
#define DPT_PRESENT_ATTEMPTED 2u
#define DPT_SOURCE_ONLY 4u
#define DPT_NO_RESULT INT64_MIN
/* All tick/count fields are cumulative since startup configuration, with
 * saturating addition. flags/last acquire/present results refer to this driver
 * Present; last fence/submit results refer to their latest observed API calls.
 * VK_SUCCESS is0; non_success counts include every other VkResult, including
 * SUBOPTIMAL/TIMEOUT. HRESULT and API success are not displayed/completed proof. */
typedef struct {
 uint64_t version,bytes,sequence,qpc_frequency,flags,saturated;
 uint64_t fence_ticks,fence_calls,fence_non_success;
 uint64_t submit_calls,submit_non_success;
 uint64_t acquire_ticks,acquire_calls,acquire_non_success;
 uint64_t present_ticks,present_calls,present_non_success;
 uint64_t command_calls,command_non_success,bad_clock_samples;
 int64_t last_fence_result,last_submit_result,last_acquire_result,last_present_result,last_hresult;
} DptSnapshot;
/* Startup once before device creation/workers. OFF has no clocks or callbacks.
 * ON queries QPC frequency once; duration samples reuse existing backend QPC
 * calls and never add timestamps, GPU calls, waits or backend IO. */
int d3d8_vk_present_timing_configure(int enabled);
/* Bounded atomic snapshot, no spin/wait/clock/GPU/guest reads. Returns0 and a
 * zeroed output if OFF, no completed API boundary, or concurrent publication.
 * Does not require nv2a_executor owner TLS; safe on actual Present caller. */
int d3d8_vk_present_timing_read(DptSnapshot*);
#ifdef __cplusplus
}
#endif
#endif
