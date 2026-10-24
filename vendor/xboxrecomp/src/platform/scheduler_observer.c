/* Bounded, default-off scheduling collector. Own work: djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "scheduler_observer.h"
#include <stdatomic.h>
#include <string.h>
#include <time.h>
static W32SchedulerRow rows[W32_SCHEDULER_ROWS];
static atomic_uint enabled,inflight,published;
static atomic_uint_fast64_t dropped,faults;
static uint64_t window_start,window_end;
static W32SchedulerClock clock_fn;
static void *clock_user;
static _Thread_local struct {W32SchedulerRow row;int active,wait_scope,failed;} tls;
static int real_clock(unsigned kind,uint64_t *value,void *user){
 (void)user;struct timespec t;clockid_t id;
 switch(kind){case W32_SCHED_MONOTONIC:id=CLOCK_MONOTONIC;break;case W32_SCHED_REALTIME:id=CLOCK_REALTIME;break;case W32_SCHED_CPU:id=CLOCK_THREAD_CPUTIME_ID;break;default:return 0;}
 if(clock_gettime(id,&t))return 0;
 *value=(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;return 1;
}
static int stamp(unsigned kind,uint64_t *out){
 if(!clock_fn(kind,out,clock_user)){atomic_fetch_add_explicit(&faults,1,memory_order_relaxed);tls.failed=1;return 0;}return 1;
}
int w32_sched_observer_configure(uint64_t begin,uint64_t end,W32SchedulerClock clock,void *user){
 if(begin>=end||atomic_load_explicit(&enabled,memory_order_acquire)||atomic_load_explicit(&inflight,memory_order_acquire))return 0;
 window_start=begin;window_end=end;clock_fn=clock?clock:real_clock;clock_user=user;
 atomic_store(&published,0);atomic_store(&dropped,0);atomic_store(&faults,0);
 atomic_store_explicit(&enabled,1,memory_order_release);return 1;
}
void w32_sched_observer_stop(void){atomic_store_explicit(&enabled,0,memory_order_release);}
int w32_sched_observer_begin(void){
 if(!atomic_load_explicit(&enabled,memory_order_acquire))return 0;
 if(tls.active){atomic_fetch_add(&faults,1);return 0;}
 uint64_t now;tls.failed=0;
 if(!stamp(W32_SCHED_MONOTONIC,&now))return 0;
 if(now<window_start)return 0;
 if(now>=window_end){w32_sched_observer_stop();return 0;}
 if(atomic_load_explicit(&published,memory_order_relaxed)==W32_SCHEDULER_ROWS){atomic_fetch_add(&dropped,1);w32_sched_observer_stop();return 0;}
 unsigned vacant=0;
 if(!atomic_compare_exchange_strong_explicit(&inflight,&vacant,1,memory_order_acquire,memory_order_relaxed)){atomic_fetch_add(&faults,1);return 0;}
 if(!atomic_load_explicit(&enabled,memory_order_acquire)){atomic_store_explicit(&inflight,0,memory_order_release);return 0;}
 memset(&tls.row,0,sizeof tls.row);tls.row.sequence=(uint64_t)atomic_load(&published)+1;tls.row.wait_begin_mono=now;
 tls.active=tls.wait_scope=1;return 1;
}
void w32_sched_observer_host_done(uint64_t deadline,uint32_t result,int blocked){
 if(!tls.active||!tls.wait_scope)return;
 tls.row.wait_deadline_real=deadline;tls.row.wait_result=result;tls.row.blocked=blocked!=0;
 if(stamp(W32_SCHED_MONOTONIC,&tls.row.cond_done_mono)&&stamp(W32_SCHED_REALTIME,&tls.row.cond_done_real))tls.row.valid|=W32_SCHED_WAIT;
}
void w32_sched_observer_host_done_monotonic(uint64_t deadline,uint32_t result,int blocked){
 if(!tls.active||!tls.wait_scope)return;
 tls.row.wait_deadline_mono=deadline;
 w32_sched_observer_host_done(0,result,blocked);
}
void w32_sched_observer_turn_begin(void){
 if(tls.active&&tls.wait_scope)stamp(W32_SCHED_MONOTONIC,&tls.row.turn_request_mono);
}
void w32_sched_observer_turn_end(void){
 if(!tls.active||!tls.wait_scope)return;
 if(stamp(W32_SCHED_MONOTONIC,&tls.row.turn_acquired_mono))tls.row.valid|=W32_SCHED_TURN;
 tls.wait_scope=0;
}
void w32_sched_observer_vblank(uint64_t sample,uint64_t deadline,uint64_t period,int fired){
 if(!tls.active)return;
 tls.row.vblank_sample_mono=sample;tls.row.vblank_deadline_mono=deadline;tls.row.vblank_period=period;tls.row.vblank_fired=fired!=0;tls.row.valid|=W32_SCHED_VBLANK;
}
void w32_sched_observer_ptimer(uint64_t sample,uint64_t count,uint32_t alarm,uint64_t rate,uint32_t num,uint32_t den,int fired,int reset){
 if(!tls.active)return;
 tls.row.ptimer_sample_mono=sample;tls.row.ptimer_count=count;tls.row.ptimer_alarm=alarm;tls.row.ptimer_rate=rate;tls.row.ptimer_num=num;tls.row.ptimer_den=den;tls.row.ptimer_fired=fired!=0;tls.row.ptimer_reset=reset!=0;tls.row.valid|=W32_SCHED_PTIMER;
}
void w32_sched_observer_service(int entering){
 if(!tls.active)return;
 if(entering)stamp(W32_SCHED_MONOTONIC,&tls.row.service_begin_mono);
 else if(stamp(W32_SCHED_MONOTONIC,&tls.row.service_end_mono))tls.row.valid|=W32_SCHED_SERVICE;
}
void w32_sched_observer_end(void){
 if(!tls.active)return;
 stamp(W32_SCHED_MONOTONIC,&tls.row.cycle_end_mono);stamp(W32_SCHED_CPU,&tls.row.thread_cpu_ns);
 if(tls.wait_scope||!(tls.row.valid&W32_SCHED_WAIT)||!(tls.row.valid&W32_SCHED_TURN)||tls.row.cond_done_mono<tls.row.wait_begin_mono||tls.row.turn_request_mono<tls.row.cond_done_mono||tls.row.turn_acquired_mono<tls.row.turn_request_mono||tls.row.cycle_end_mono<tls.row.turn_acquired_mono||((tls.row.valid&W32_SCHED_SERVICE)&&tls.row.service_end_mono<tls.row.service_begin_mono))tls.failed=1;
 if(tls.failed)atomic_fetch_add(&dropped,1);
 else{unsigned index=atomic_load_explicit(&published,memory_order_relaxed);rows[index]=tls.row;atomic_store_explicit(&published,index+1,memory_order_release);}
 tls.active=tls.wait_scope=0;atomic_store_explicit(&inflight,0,memory_order_release);
}
size_t w32_sched_observer_copy(W32SchedulerRow *out,size_t capacity,W32SchedulerStats *stats){
 unsigned n=atomic_load_explicit(&published,memory_order_acquire);
 if(stats)*stats=(W32SchedulerStats){n,atomic_load(&dropped),atomic_load(&faults),atomic_load(&enabled),atomic_load(&inflight)};
 if(!out)return 0;
 if(capacity<n)n=(unsigned)capacity;
 memcpy(out,rows,(size_t)n*sizeof rows[0]);return n;
}
