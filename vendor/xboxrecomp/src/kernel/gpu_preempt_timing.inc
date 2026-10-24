/* Included once before original kernel_gpu_preempt. Own work: djultra64. */
#include "gpu_preempt_timing.h"
static int gpt_enabled,gpt_configured;
static RECOMP_TLS GpuPreemptTiming gpt_tls;
int xbox_GpuPreemptTimingConfigure(int enabled){
 if(gpt_configured||(enabled!=0&&enabled!=1))return 0;
 gpt_enabled=enabled;gpt_configured=1;return 1;
}
int xbox_GpuPreemptTimingRead(GpuPreemptTiming*out){
 if(!gpt_enabled||!out)return 0;
 *out=gpt_tls;out->version=GPU_PREEMPT_TIMING_VERSION;return 1;
}
static void gpt_add_us(LONGLONG duration_us){
 /* Caller passes the EXACT duration expression already used by original stats.
  * Never sample another clock, recompute a deadline or modify guest state. */
 if(gpt_tls.holds==UINT64_MAX)gpt_tls.flags|=GPU_PREEMPT_TIMING_SATURATED;else gpt_tls.holds++;
 if(duration_us<0){gpt_tls.flags|=GPU_PREEMPT_TIMING_BAD_DURATION;return;}
 uint64_t us=(uint64_t)duration_us;
 if(us>UINT64_MAX-gpt_tls.total_us){gpt_tls.total_us=UINT64_MAX;gpt_tls.flags|=GPU_PREEMPT_TIMING_SATURATED;}else gpt_tls.total_us+=us;
}
