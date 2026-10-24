/* Own work djultra64. Pure selection, not a clock or IRQ write. */
#ifndef TOPSPIN_TIMER_WAIT_BUDGET_H
#define TOPSPIN_TIMER_WAIT_BUDGET_H
#include <stdint.h>
static inline uint64_t topspin_timer_wait_budget(uint64_t now,uint64_t deadline,uint64_t frequency){
 if(!deadline||!frequency)return 1000000;
 if(deadline<=now)return 0;
 /* Exact ceiling with an overflow-safe128-bit intermediate on POSIX GCC/Clang.
  * A fractional positive deadline never rounds to zero/spins. */
 __uint128_t value=(__uint128_t)(deadline-now)*1000000000u;
 value=(value+frequency-1)/frequency;
 return value>1000000?1000000:(uint64_t)value;
}
#endif
