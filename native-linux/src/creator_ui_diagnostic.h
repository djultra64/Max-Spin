/* Read-only original Creator UI observation. Own work: djultra64. */
#ifndef TOPSPIN_CREATOR_UI_DIAGNOSTIC_H
#define TOPSPIN_CREATOR_UI_DIAGNOSTIC_H
#include <stdint.h>
/* Calling guest thread, after an original callback or periodic trusted seam.
 * Exact TOPSPIN_CREATOR_UI_DIAGNOSTICS=1 only; at most12 changed states/process
 * plus one existing ready HOST bridge failure and one ready tracking failure.
 * No guest calls/writes, input injection, UI activation or output files.
 * Complete guest CPU/host fenv are preserved, including around stderr logging. */
void topspin_creator_ui_observe(uint32_t loading,uint32_t context,uint32_t manager,uint32_t hub);
#endif
