/* Host main-thread adapter, opt-in only. Own work djultra64. */
#ifndef TOPSPIN_SCHEDULER_LATENESS_TRACE_H
#define TOPSPIN_SCHEDULER_LATENESS_TRACE_H
#include <stdint.h>
/* Before workers, elapsed_ns from the same SDL ticks epoch used by frame CSV.
 * Absent TOPSPIN_SCHEDULER_TRACE: no clocks, file access or collector activation. */
int topspin_scheduler_lateness_active(void);
int topspin_scheduler_lateness_init(uint64_t elapsed_ns);
/* Host main, outside guest_turn. Does not wait on guest, SDL, GPU or mutexes.
 * Dumps once after duration/full or force. No existing destination overwritten. */
int topspin_scheduler_lateness_poll(uint64_t elapsed_ns,int force);
#endif
