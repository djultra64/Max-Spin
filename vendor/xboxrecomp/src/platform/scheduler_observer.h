/* Optional bounded host scheduling observations. Own work: djultra64. */
#ifndef W32_SCHEDULER_OBSERVER_H
#define W32_SCHEDULER_OBSERVER_H
#include <stdint.h>
#include <stddef.h>
#define W32_SCHEDULER_MONOTONIC_WAIT 1
#define W32_SCHEDULER_ROWS 8192u
enum {W32_SCHED_MONOTONIC=1,W32_SCHED_REALTIME=2,W32_SCHED_CPU=3};
enum {W32_SCHED_WAIT=1,W32_SCHED_TURN=2,W32_SCHED_VBLANK=4,W32_SCHED_PTIMER=8,W32_SCHED_SERVICE=16};
typedef struct W32SchedulerRow {
 uint64_t sequence,wait_begin_mono,wait_deadline_real,cond_done_mono,cond_done_real;
 uint64_t turn_request_mono,turn_acquired_mono,service_begin_mono,service_end_mono,cycle_end_mono,thread_cpu_ns;
 uint64_t vblank_sample_mono,vblank_deadline_mono,vblank_period;
 uint64_t ptimer_sample_mono,ptimer_count,ptimer_rate;
 uint32_t valid,wait_result,blocked,vblank_fired,ptimer_alarm,ptimer_num,ptimer_den,ptimer_fired,ptimer_reset;
 /* Zero for existing REALTIME waits; never reinterpret either clock domain. */
 uint64_t wait_deadline_mono;
} W32SchedulerRow;
typedef struct W32SchedulerStats {uint64_t published,dropped,faults;unsigned enabled,inflight;} W32SchedulerStats;
typedef int (*W32SchedulerClock)(unsigned kind,uint64_t *value,void *user);
#ifdef __cplusplus
extern "C" {
#endif
/* Configure before workers or after stop+joined producer. NULL clock selects
 * POSIX clocks. No environment/IO and no guest memory or timing writes. */
int w32_sched_observer_configure(uint64_t start_mono,uint64_t end_mono,W32SchedulerClock,void*);
void w32_sched_observer_stop(void);
/* Only the timer-service thread calls begin/end. Platform wait hooks use TLS,
 * so unrelated waits/threads do not create observations. Default OFF: no clocks. */
int w32_sched_observer_begin(void);
void w32_sched_observer_host_done(uint64_t deadline_real,uint32_t result,int blocked);
void w32_sched_observer_host_done_monotonic(uint64_t deadline_mono,uint32_t result,int blocked);
void w32_sched_observer_turn_begin(void);
void w32_sched_observer_turn_end(void);
void w32_sched_observer_vblank(uint64_t sample,uint64_t deadline,uint64_t period,int fired);
void w32_sched_observer_ptimer(uint64_t sample,uint64_t count,uint32_t alarm,uint64_t rate,uint32_t num,uint32_t den,int fired,int reset);
void w32_sched_observer_service(int entering);
void w32_sched_observer_end(void);
/* Main thread only: immutable published prefix. No allocation, file IO, locks,
 * or waiting. Returns copied rows; stats disclose pending/lost/faulted data. */
size_t w32_sched_observer_copy(W32SchedulerRow *out,size_t capacity,W32SchedulerStats *stats);
#ifdef __cplusplus
}
#endif
#endif
