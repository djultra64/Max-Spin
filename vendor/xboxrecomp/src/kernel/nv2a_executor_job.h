/* Bounded host-only work on the NV2A executor. Own work: djultra64. */
#ifndef XBOXRECOMP_NV2A_EXECUTOR_JOB_H
#define XBOXRECOMP_NV2A_EXECUTOR_JOB_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NV2A_EXECUTOR_INPUT_MAX 4096u
#define NV2A_EXECUTOR_OUTPUT_MAX (2u*1024u*1024u)
enum {NV2A_EXECUTOR_DONE=1,NV2A_EXECUTOR_DISABLED=0,NV2A_EXECUTOR_TIMEOUT=-1,NV2A_EXECUTOR_ABORTED=-2,NV2A_EXECUTOR_BUSY=-3,NV2A_EXECUTOR_INVALID=-4,NV2A_EXECUTOR_REENTRANT=-5,NV2A_EXECUTOR_NOMEM=-6};
/* Trusted HOST callback. Both buffers are owned by the job; no guest pointer,
 * borrowed caller storage, or renderer resource may be retained by callback.
 * All resolve/query/copy/submit/readback work belongs inside this one callback.
 * Callback must return normally, and must not synchronously invoke another job.
 * A timeout/abort does NOT cancel running GPU work or authorize device teardown. */
typedef int (*Nv2aExecutorWork)(const void *input,uint32_t input_bytes,void *output,uint32_t output_bytes);
/* Called only by the real ack thread at its lifetime and outer safe boundaries.
 * Exact RECOMP_EXECUTOR_INVOKE=1 required. A service executes at most one job. */
int nv2a_executor_jobs_start(void);
/* TLS owner identity; useful to reject exporter calls off the GPU thread. */
int nv2a_executor_is_owner(void);
int nv2a_executor_jobs_service(void);
/* Any host thread may abort admission/waiters. Running work retains owned
 * storage until the executor completes. Owner shutdown calls this too. */
void nv2a_executor_jobs_stop(void);
/* Synchronous HOST request. Input copied before publication; outputs and result
 * are unchanged unless DONE. One pending/running job process-wide. Does not
 * drive the title or GPU from the calling thread. CLI/highIRQL callers reject.
 * Caller must keep the renderer alive until executor shutdown actually joins. */
int nv2a_executor_invoke(Nv2aExecutorWork work,const void *input,uint32_t input_bytes,void *output,uint32_t output_bytes,uint32_t timeout_ms,int *callback_result);
/* Same owned request, admission, deadline and cancellation contract as invoke.
 * Pending work dispatches only when the real executor outer boundary observes
 * stable PUT==GET, no outstanding SWM, through xbox_Nv2aDrainSnapshot. A failed
 * predicate leaves the slot pending and returns to original PB processing.
 * Does not freeze later guest submissions: work must still revalidate drain
 * before publishing its own result. Old invoke performs no drain observation. */
int nv2a_executor_invoke_drained(Nv2aExecutorWork work,const void *input,uint32_t input_bytes,void *output,uint32_t output_bytes,uint32_t timeout_ms,int *callback_result);
#ifdef __cplusplus
}
#endif
#endif
