/* Actual backend-only producer, atomic bounded readers. djultra64. */
#include "driver_present_timing.h"
#include <stdatomic.h>
#include <string.h>
#include <limits.h>
enum {DPT_FENCE=1,DPT_SUBMIT,DPT_ACQUIRE,DPT_QUEUE_PRESENT,DPT_COMMAND};
static int dpt_configured,dpt_on;
static uint64_t dpt_frequency;
static DptSnapshot dpt_live;
#define DPT_CELLS (sizeof(DptSnapshot)/sizeof(uint64_t))
_Static_assert(sizeof(DptSnapshot)==25u*sizeof(uint64_t),"timing snapshot wire layout");
static _Atomic uint64_t dpt_guard,dpt_cells[DPT_CELLS];
static atomic_int dpt_publication_failed;
int d3d8_vk_present_timing_configure(int enabled){
 if(dpt_configured||vk.ready||(enabled!=0&&enabled!=1))return 0;
 dpt_configured=1;if(!enabled)return 1;
 LARGE_INTEGER f;if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
 dpt_live.last_fence_result=dpt_live.last_submit_result=dpt_live.last_acquire_result=dpt_live.last_present_result=DPT_NO_RESULT;
 dpt_frequency=(uint64_t)f.QuadPart;dpt_on=1;return 1;
}
static void dpt_add(uint64_t*p,uint64_t n){if(UINT64_MAX-*p<n){*p=UINT64_MAX;dpt_live.saturated=1;}else *p+=n;}
static void dpt_result(unsigned op,int32_t result){
 if(!dpt_on)return;
 switch(op){
 case DPT_FENCE:dpt_add(&dpt_live.fence_calls,1);if(result)dpt_add(&dpt_live.fence_non_success,1);dpt_live.last_fence_result=result;break;
 case DPT_SUBMIT:dpt_add(&dpt_live.submit_calls,1);if(result)dpt_add(&dpt_live.submit_non_success,1);dpt_live.last_submit_result=result;break;
 case DPT_ACQUIRE:dpt_add(&dpt_live.acquire_calls,1);if(result)dpt_add(&dpt_live.acquire_non_success,1);dpt_live.last_acquire_result=result;dpt_live.flags|=DPT_ACQUIRE_ATTEMPTED;break;
 case DPT_QUEUE_PRESENT:dpt_add(&dpt_live.present_calls,1);if(result)dpt_add(&dpt_live.present_non_success,1);dpt_live.last_present_result=result;dpt_live.flags|=DPT_PRESENT_ATTEMPTED;break;
 case DPT_COMMAND:dpt_add(&dpt_live.command_calls,1);if(result)dpt_add(&dpt_live.command_non_success,1);break;
 }
}
static void dpt_ticks(unsigned op,int valid,int64_t a,int64_t b){
 if(!dpt_on)return;
 if(!valid||a<0||b<a){dpt_add(&dpt_live.bad_clock_samples,1);return;}
 uint64_t ticks=(uint64_t)b-(uint64_t)a;
 if(op==DPT_FENCE)dpt_add(&dpt_live.fence_ticks,ticks);
 else if(op==DPT_ACQUIRE)dpt_add(&dpt_live.acquire_ticks,ticks);
 else if(op==DPT_QUEUE_PRESENT)dpt_add(&dpt_live.present_ticks,ticks);
}
static void dpt_begin_present(void){if(!dpt_on)return;dpt_live.flags=0;dpt_live.last_acquire_result=dpt_live.last_present_result=DPT_NO_RESULT;}
static void dpt_source_only(void){if(dpt_on)dpt_live.flags|=DPT_SOURCE_ONLY;}
static void dpt_end_present(int32_t hr){
 if(!dpt_on)return;
 uint64_t seq=atomic_load_explicit(&dpt_guard,memory_order_relaxed);
 if(seq>UINT64_MAX-2||dpt_live.sequence==UINT64_MAX){atomic_store_explicit(&dpt_publication_failed,1,memory_order_release);return;}
 dpt_live.version=DPT_VERSION;dpt_live.bytes=sizeof dpt_live;dpt_live.qpc_frequency=dpt_frequency;dpt_live.sequence++;dpt_live.last_hresult=hr;
 uint64_t values[DPT_CELLS];memcpy(values,&dpt_live,sizeof values);
 atomic_store_explicit(&dpt_guard,seq+1,memory_order_relaxed);
 /* Atomic data cells and these fences avoid a C data race as well as torn
  * snapshots. A reader returns MISSING rather than spin on an active writer. */
 atomic_thread_fence(memory_order_release);
 for(unsigned i=0;i<DPT_CELLS;i++)atomic_store_explicit(dpt_cells+i,values[i],memory_order_relaxed);
 atomic_store_explicit(&dpt_guard,seq+2,memory_order_release);
}
int d3d8_vk_present_timing_read(DptSnapshot*out){
 if(out)memset(out,0,sizeof*out);
 if(!out||!dpt_on||atomic_load_explicit(&dpt_publication_failed,memory_order_acquire))return 0;
 uint64_t first=atomic_load_explicit(&dpt_guard,memory_order_acquire);if(!first||(first&1))return 0;
 uint64_t values[DPT_CELLS];for(unsigned i=0;i<DPT_CELLS;i++)values[i]=atomic_load_explicit(dpt_cells+i,memory_order_relaxed);
 atomic_thread_fence(memory_order_acquire);
 if(first!=atomic_load_explicit(&dpt_guard,memory_order_relaxed))return 0;
 memcpy(out,values,sizeof*out);return 1;
}
/* Same single-evaluation/error-log semantics as original VKC; observation does
 * not turn a successful wait into a GPU or display completion certificate. */
#define DPT_VKC(op,call) do { VkResult r_=(call); if(r_!=VK_SUCCESS){ \
 LOG("%s failed (%d) at line %d",#call,(int)r_,__LINE__); } dpt_result((op),(int32_t)r_); }while(0)
