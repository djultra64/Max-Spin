/* Host visual requests only; never a simulation clock. Own work: djultra64. */
#ifndef TOPSPIN_VISUAL_OUTPUT_CLOCK_H
#define TOPSPIN_VISUAL_OUTPUT_CLOCK_H
#include <stdint.h>
enum {VOC_COMPLETE=1u,VOC_INTERPOLABLE=2u};
enum {VOC_WAIT=0,VOC_SAMPLE=1,VOC_INVALID=-1,VOC_OWNER=-2,VOC_BUSY=-3,VOC_STALE=-4,VOC_OVERFLOW=-5};
typedef struct {
 uint64_t device,scene,view,domain,serial,step,sample_ns,published_ns;
 uint32_t flags;
} VocFrameInfo;
typedef struct {
 void *user;
 int (*owner)(void*);
 /* Host-owned sealed frames only. retain/inspect never read guest memory.
  * A lease retains all frame payloads/resources. No success with a NULL lease.
  * inspect validates the exact retained version, not the current live frame. */
 int (*retain)(void*,const void *frame,void **lease);
 int (*inspect)(void*,const void *frame,void *lease,VocFrameInfo*);
 void (*release)(void*,const void *frame,void *lease);
} VocProvider;
typedef struct VocClock VocClock;
typedef struct {
 uint64_t ticket;
 const void *previous,*current;
 uint64_t previous_serial,current_serial,alpha_num,alpha_den;
} VocSample;
/* One executor owner, no reentry. Rates60/90/120 or0(unlimited demand).
 * This module neither submits rendering nor proves GPU completion. */
int voc_create(const VocProvider*,uint32_t rate,uint64_t now,VocClock**);
int voc_offer(VocClock*,const void *sealed_frame);
int voc_poll(VocClock*,uint64_t now,VocSample*,uint64_t *next_wake);
/* finish only after the real consumer fence (or before any submission on
 * failure). No offer/rate change/destruction is admitted while in flight. */
int voc_finish(VocClock*,uint64_t ticket,int success);
int voc_rate(VocClock*,uint32_t rate,uint64_t now);
int voc_destroy(VocClock**);
#endif
