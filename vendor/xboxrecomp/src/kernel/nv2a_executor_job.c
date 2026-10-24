/* Bounded owned host jobs, serviced only on the NV2A ack executor.
 * Own work: djultra64. No guest callbacks, GPU API or renderer lock here. */
#include "nv2a_executor_job.h"
#include "resource_epoch_api.h"
#if defined(_WIN32)
#include <windows.h>
#define JOB_TLS __declspec(thread)
#else
#include "../platform/win32_compat.h"
#include <pthread.h>
#define JOB_TLS _Thread_local
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
extern int xbox_IrqlPreemptible(void);
typedef enum {JOB_PENDING,JOB_RUNNING,JOB_DONE} JobState;
typedef struct Job {Nv2aExecutorWork work;uint32_t input_bytes,output_bytes;unsigned refs;JobState state;int aborted,result,requires_drain;size_t allocated,output_at;_Alignas(max_align_t) unsigned char data[];} Job;
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION lock;
static CONDITION_VARIABLE changed;
static Job *slot;
static size_t live_bytes;
static unsigned epoch;
static int accepting,owner_online;
static atomic_int enabled,ready;
static JOB_TLS unsigned owner_epoch;
static JOB_TLS int in_callback;
static BOOL WINAPI init(PINIT_ONCE o,PVOID p,PVOID*c){(void)o;(void)p;(void)c;InitializeCriticalSection(&lock);InitializeConditionVariable(&changed);return TRUE;}
static void release_job(Job*j){if(!--j->refs){live_bytes-=j->allocated;free(j);}}
static int leave_guest(void){
#if !defined(_WIN32)
 return guest_turn_leave();
#else
 return 0;
#endif
}
static void return_guest(int left){
#if !defined(_WIN32)
 guest_turn_return(left);
#else
 (void)left;
#endif
}
int nv2a_executor_is_owner(void){return owner_epoch!=0;}
int nv2a_executor_jobs_start(void){
 const char*e=getenv("RECOMP_EXECUTOR_INVOKE");if(!e||strcmp(e,"1"))return 0;
 InitOnceExecuteOnce(&once,init,NULL,NULL);EnterCriticalSection(&lock);
 if(slot||accepting||owner_online||live_bytes){LeaveCriticalSection(&lock);return 0;}
 accepting=1;owner_online=1;epoch++;if(!epoch)epoch++;owner_epoch=epoch;atomic_store_explicit(&enabled,1,memory_order_release);LeaveCriticalSection(&lock);return 1;
}
#if !defined(_WIN32)
/* Deferred pthread cancellation of a waiting guest (TerminateThread) occurs
 * with the condition mutex reacquired. Release both ownership and that mutex;
 * the already-released guest token is handled by the platform thread cleanup. */
static void cancel_waiter(void*p){
 Job*j=p;j->aborted=1;
 if(j->state==JOB_PENDING&&slot==j){slot=NULL;atomic_store_explicit(&ready,0,memory_order_release);release_job(j);}
 release_job(j);LeaveCriticalSection(&lock);
}
#endif
static int invoke(int requires_drain,Nv2aExecutorWork work,const void*input,uint32_t in,void*output,uint32_t out,uint32_t timeout,int*result){
 if(owner_epoch||in_callback)return NV2A_EXECUTOR_REENTRANT;
 if(!atomic_load_explicit(&enabled,memory_order_acquire))return NV2A_EXECUTOR_DISABLED;
 if(!work||!input||!output||!in||in>NV2A_EXECUTOR_INPUT_MAX||!out||out>NV2A_EXECUTOR_OUTPUT_MAX||!timeout||timeout>10000u||!xbox_IrqlPreemptible())return NV2A_EXECUTOR_INVALID;
#if !defined(_WIN32)
 if(!guest_turn_interrupts_enabled())return NV2A_EXECUTOR_INVALID;
#else
 /* The optional legacy Windows guest lock has no public suspend/resume API.
  * Fail closed rather than blocking while holding that guest lock. */
 {const char*g=getenv("RECOMP_GUEST_LOCK");if(g&&*g=='1')return NV2A_EXECUTOR_INVALID;}
#endif
 /* Drop the guest token BEFORE acquiring any job/condition mutex, and regain
  * it only AFTER release. SleepConditionVariableCS otherwise reacquires a
  * mutex before gc_unblock, inverting it against another guest invoker. */
 int left=leave_guest();EnterCriticalSection(&lock);
 if(!accepting){LeaveCriticalSection(&lock);return_guest(left);return NV2A_EXECUTOR_DISABLED;}
 if(slot||live_bytes){LeaveCriticalSection(&lock);return_guest(left);return NV2A_EXECUTOR_BUSY;}
 size_t alignment=_Alignof(max_align_t),output_at=((size_t)in+alignment-1)/alignment*alignment;
 size_t bytes=sizeof(Job)+output_at+out;Job*j=calloc(1,bytes);
 if(!j){LeaveCriticalSection(&lock);return_guest(left);return NV2A_EXECUTOR_NOMEM;}
 j->requires_drain=requires_drain;j->work=work;j->input_bytes=in;j->output_bytes=out;j->refs=2;j->state=JOB_PENDING;j->allocated=bytes;j->output_at=output_at;memcpy(j->data,input,in);live_bytes+=bytes;slot=j;atomic_store_explicit(&ready,1,memory_order_release);
 uint64_t deadline=GetTickCount64()+timeout;int status;
#if !defined(_WIN32)
 pthread_cleanup_push(cancel_waiter,j);
#endif
 for(;;){
  if(j->aborted){status=NV2A_EXECUTOR_ABORTED;break;}
  if(j->state==JOB_DONE){memcpy(output,j->data+j->output_at,out);if(result)*result=j->result;status=NV2A_EXECUTOR_DONE;break;}
  uint64_t now=GetTickCount64();if(now>=deadline){status=NV2A_EXECUTOR_TIMEOUT;break;}
  uint64_t remaining=deadline-now;SleepConditionVariableCS(&changed,&lock,(DWORD)remaining);
 }
 if(j->state==JOB_PENDING){
  /* A timed-out/aborted request has never run; unlink its queue ownership.
   * The caller's reference keeps its condition predicate alive until below. */
  if(slot==j){slot=NULL;atomic_store_explicit(&ready,0,memory_order_release);release_job(j);}
 }
 release_job(j);LeaveCriticalSection(&lock);
#if !defined(_WIN32)
 pthread_cleanup_pop(0);
#endif
 return_guest(left);return status;
}
int nv2a_executor_invoke(Nv2aExecutorWork work,const void*input,uint32_t in,void*output,uint32_t out,uint32_t timeout,int*result){
 return invoke(0,work,input,in,output,out,timeout,result);
}
int nv2a_executor_invoke_drained(Nv2aExecutorWork work,const void*input,uint32_t in,void*output,uint32_t out,uint32_t timeout,int*result){
 return invoke(1,work,input,in,output,out,timeout,result);
}
int nv2a_executor_jobs_service(void){
 if(in_callback)return NV2A_EXECUTOR_REENTRANT;
 if(!owner_epoch)return NV2A_EXECUTOR_INVALID;
 if(!atomic_load_explicit(&enabled,memory_order_acquire)||!atomic_load_explicit(&ready,memory_order_acquire))return 0;
 EnterCriticalSection(&lock);
 if(owner_epoch!=epoch||!accepting||!slot||slot->state!=JOB_PENDING){LeaveCriticalSection(&lock);return 0;}
 Job*j=slot;
 if(j->aborted){slot=NULL;atomic_store_explicit(&ready,0,memory_order_release);release_job(j);WakeAllConditionVariable(&changed);LeaveCriticalSection(&lock);return 0;}
 /* Keep the same queue owner/reference and ready predicate while original PB
  * consumption advances. Never wait or invoke work before real drain proof.
  * Cancellation/shutdown may unlink this pending job under the same lock. */
 if(j->requires_drain){XboxNv2aDrainState drained;
  if(!xbox_Nv2aDrainSnapshot(&drained)){LeaveCriticalSection(&lock);return 0;}
 }
 j->state=JOB_RUNNING;atomic_store_explicit(&ready,0,memory_order_release);LeaveCriticalSection(&lock);
 in_callback=1;int result=j->work(j->data,j->input_bytes,j->data+j->output_at,j->output_bytes);in_callback=0;
 EnterCriticalSection(&lock);j->result=result;j->state=JOB_DONE;if(slot==j)slot=NULL;WakeAllConditionVariable(&changed);release_job(j);LeaveCriticalSection(&lock);return 1;
}
void nv2a_executor_jobs_stop(void){
 if(!atomic_load_explicit(&enabled,memory_order_acquire)&&!owner_epoch)return;
 EnterCriticalSection(&lock);
 if(owner_epoch&&owner_epoch!=epoch){owner_epoch=0;LeaveCriticalSection(&lock);return;}
 if(owner_epoch==epoch&&owner_epoch)owner_online=0;
 accepting=0;atomic_store_explicit(&enabled,0,memory_order_release);atomic_store_explicit(&ready,0,memory_order_release);
 Job*j=slot;if(j){j->aborted=1;if(j->state==JOB_PENDING){slot=NULL;release_job(j);}WakeAllConditionVariable(&changed);}
 LeaveCriticalSection(&lock);owner_epoch=0;
}
#ifdef NV2A_EXECUTOR_JOB_TEST
int nv2a_executor_job_test_pending(void){int n;EnterCriticalSection(&lock);n=slot!=NULL;LeaveCriticalSection(&lock);return n;}
size_t nv2a_executor_job_test_live(void){size_t n;EnterCriticalSection(&lock);n=live_bytes;LeaveCriticalSection(&lock);return n;}
#endif
