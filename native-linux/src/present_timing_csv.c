/* Startup opt-in; all driver fields are observations, not GPU completion. djultra64. */
#include "present_timing_csv.h"
#include "driver_present_timing.h"
static int configured,extended;
int topspin_present_csv_init(int enabled){if(configured||(enabled!=0&&enabled!=1))return 0;configured=1;if(!enabled)return 1;if(!d3d8_vk_present_timing_configure(1))return 0;extended=1;return 1;}
void topspin_present_csv_header(FILE*f){if(!f)return;if(!extended){fputs("frame,present_ns,interval_ns,guest_swap,guest_interval\n",f);return;}fputs("frame,present_ns,interval_ns,guest_swap,guest_interval,vkpt_valid,vkpt_sequence,vkpt_qpc_frequency,vkpt_flags,vkpt_saturated,vkpt_fence_ticks,vkpt_fence_calls,vkpt_fence_non_success,vkpt_submit_calls,vkpt_submit_non_success,vkpt_acquire_ticks,vkpt_acquire_calls,vkpt_acquire_non_success,vkpt_present_ticks,vkpt_present_calls,vkpt_present_non_success,vkpt_command_calls,vkpt_command_non_success,vkpt_bad_clock_samples,vkpt_last_fence_result,vkpt_last_submit_result,vkpt_last_acquire_result,vkpt_last_present_result,vkpt_last_hresult\n",f);}
void topspin_present_csv_row(FILE*f,uint64_t frame,uint64_t now,uint64_t interval,uint32_t swap,uint32_t guest_interval){
 if(!f)return;
 if(!extended){fprintf(f,"%llu,%llu,%llu,%u,%u\n",(unsigned long long)frame,(unsigned long long)now,(unsigned long long)interval,swap,guest_interval);return;}
 DptSnapshot s;int valid=d3d8_vk_present_timing_read(&s);
 fprintf(f,"%llu,%llu,%llu,%u,%u,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%lld,%lld,%lld,%lld,%lld\n",(unsigned long long)frame,(unsigned long long)now,(unsigned long long)interval,swap,guest_interval,valid,
  (unsigned long long)s.sequence,
  (unsigned long long)s.qpc_frequency,
  (unsigned long long)s.flags,
  (unsigned long long)s.saturated,
  (unsigned long long)s.fence_ticks,
  (unsigned long long)s.fence_calls,
  (unsigned long long)s.fence_non_success,
  (unsigned long long)s.submit_calls,
  (unsigned long long)s.submit_non_success,
  (unsigned long long)s.acquire_ticks,
  (unsigned long long)s.acquire_calls,
  (unsigned long long)s.acquire_non_success,
  (unsigned long long)s.present_ticks,
  (unsigned long long)s.present_calls,
  (unsigned long long)s.present_non_success,
  (unsigned long long)s.command_calls,
  (unsigned long long)s.command_non_success,
  (unsigned long long)s.bad_clock_samples,
  (long long)s.last_fence_result,
  (long long)s.last_submit_result,
  (long long)s.last_acquire_result,
  (long long)s.last_present_result,
  (long long)s.last_hresult);
}
