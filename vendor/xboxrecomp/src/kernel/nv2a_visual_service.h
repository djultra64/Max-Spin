/* Deadline service at real executor idle boundary. Own work: djultra64. */
#ifndef XBOXRECOMP_NV2A_VISUAL_SERVICE_H
#define XBOXRECOMP_NV2A_VISUAL_SERVICE_H
#include <stdint.h>
typedef struct {
 void*user;int(*owner)(void*);uint64_t(*now_ns)(void*);
 /* Nonblocking, one output stage at most. No guest callbacks, waits, updates,
  * IRQs, input/audio, original Present, or nested service calls. UINT64_MAX
  * means dormant until a new owned frame/fence/setting notification. */
 int(*run)(void*,uint64_t now_ns,uint64_t*next_ns);
 /* Owner-thread revocation only; must retain ambiguous in-flight resources. */
 void(*stop)(void*);
} Nv2aVisualService;
int nv2a_visual_configure(const Nv2aVisualService*,int enabled);
/* Actual ack thread only, even when no service was configured. */
int nv2a_visual_start(void);
/* Original ack owner only; OFF avoids even the extra live PUT read. */
int nv2a_visual_enabled(void);
/* Any publisher may notify; no GPU, clock, guest read, lock or callback. */
void nv2a_visual_notify(void);
/* actual_idle must come from final live hardwarePUT vs real executor cursor,
 * outside parser and after original housekeeping. Never changes guest cadence. */
int nv2a_visual_poll(int actual_idle);
/* Any thread can revoke; only actual owner stop invokes host stop callback. */
void nv2a_visual_revoke(void);
void nv2a_visual_stop(void);
#endif
