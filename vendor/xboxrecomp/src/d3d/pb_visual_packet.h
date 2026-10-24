/* Proposed one actual PB submission -> immutable packet. Own work djultra64. */
#ifndef TOPSPIN_PB_VISUAL_PACKET_H
#define TOPSPIN_PB_VISUAL_PACKET_H
#include "visual_frame_packet.h"
typedef struct {
 void*user;
 int(*owner)(void*);
 /* Actual frontend scene/view lifecycle authority, NOT a guessed frame number. */
 int(*generation)(void*,VfpGeneration*);
 /* Incremented by actual backend vkCmdDraw[Indexed], never a PGRAPH success code. */
 uint64_t(*draw_serial)(void*);
 int(*capture)(void*,VfpBudget*,const VfpSource*,VfpPacket**);
 int(*release)(void*,VfpPacket**);
} PbVisualBackend;
typedef struct {uint32_t version,program_hash;} PbVisualRequest;
typedef struct {uint32_t state,result;uint64_t ticket;} PbVisualStatus;
/* Before execution only, single executor owner, no reentry. Every callback is
 * required and must outlive packet. Runtime remains OFF without an arm job. */
int pb_visual_configure(const PbVisualBackend*);
/* These HOST-buffer callbacks match Nv2aExecutorWork93 exactly. No pointer or
 * guest source crosses the job boundary; input is only an exact program key. */
int pb_visual_arm_job(const void*,uint32_t,void*,uint32_t);
int pb_visual_release_job(const void*,uint32_t,void*,uint32_t);
int pb_visual_interested(uint32_t hash);
uint64_t pb_visual_before(uint32_t hash);
/* Called after ORIGINAL draw once. Borrowed arrays copied synchronously on GPU
 * owner; successful snapshot flushes original commands without redrawing. */
int pb_visual_after(uint64_t,const VfpSource*);
/* Borrowed immutable packet, owner-only. Consumer must register its real GPU
 * fences before releasing via release_job. No host thread may use this pointer. */
const VfpPacket*pb_visual_owned(void);
#endif
