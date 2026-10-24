/* The POSIX scheduler observer has no native Windows collector. djultra64. */
#include "scheduler_lateness_trace.h"
#include <stdlib.h>
#include <stdio.h>
int topspin_scheduler_lateness_active(void){return 0;}
int topspin_scheduler_lateness_init(uint64_t now){(void)now;if(getenv("TOPSPIN_SCHEDULER_TRACE")){fputs("[SCHEDULER] POSIX observer is unavailable on native Windows\n",stderr);return 0;}return 1;}
int topspin_scheduler_lateness_poll(uint64_t now,int force){(void)now;(void)force;return 1;}
