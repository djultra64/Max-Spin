/* Owned complete scenes -> visual clock; no rendering or guest work. djultra64. */
#ifndef TOPSPIN_SCENE_OUTPUT_FRAMES_H
#define TOPSPIN_SCENE_OUTPUT_FRAMES_H
#include "visual_scene_journal.h"
#include "visual_output_clock.h"
#define VSO_POLICY_BYTES 8192u
typedef struct VsoOutput VsoOutput;
typedef struct {
 void*user;int(*owner)(void*);
 /* Optional actual visual-binding authority. 1 means all required pose inputs
  * and their exact draw/program lineage are owned by this frame; set a nonzero
  * compatibility domain. 0 is discrete. Negative rejects. Called again before
  * submission; no guest reads/jobs. NULL keeps all frames discrete. This API
  * does not itself establish actor binding or implement interpolation. */
 int(*attest)(void*,const VsjFrame*,uint64_t*domain);
 uint32_t rate;
} VsoConfig;
/* Separate immutable startup configuration; VsoConfig ABI is unchanged.
 * A sidecar must own all host proof/input payloads its authority needs. */
typedef struct {
 void*user;
 int(*attest)(void*,const VsjFrame*,const void*aux,uint64_t*domain);
 void(*release)(void*,void*aux);
} VsoAuxConfig;
typedef struct {
 uint64_t ticket,alpha_num,alpha_den;
 const VsjFrame*previous,*current;
 const void*policy;size_t policy_bytes;
 const void*previous_aux,*current_aux;
} VsoSample;
VsoOutput*vso_create(const VsoConfig*);
/* Once before first adoption/enqueue attempt; synchronous actual owner only. */
int vso_aux_configure(VsoOutput*,const VsoAuxConfig*);
/* Success adopts both pointers, clears both. Any failure preserves both.
 * Uses configured sidecar authority; legacy frames with no aux are discrete. */
int vso_adopt_visual(VsoOutput*,VsjFrame**,void**aux,uint64_t sample_ns,uint64_t published_ns,const void*policy,size_t bytes);
/* During an actual in-flight A/B ticket, adopts one pending C without calling
 * the visual clock. A fourth frame is BUSY with ownership untouched.
 * Pending C is offered only inside finish after the actual consumer fence.
 * Invalid pending C is released after finish and clears the obsolete clock. */
int vso_enqueue_visual(VsoOutput*,VsjFrame**,void**aux,uint64_t sample_ns,uint64_t published_ns,const void*policy,size_t bytes);
/* On success adopts *frame and sets it NULL; on failure adopts nothing.
 * Policy is synchronously copied. Retains previous/current without guest ptrs.
 * sample_ns is original sampled time, published_ns is host completion time. */
int vso_adopt(VsoOutput*,VsjFrame**,uint64_t sample_ns,uint64_t published_ns,const void*policy,size_t bytes);
int vso_poll(VsoOutput*,uint64_t now,VsoSample*,uint64_t*next_wake);
/* Check immediately before displaying a submitted result; revocation may
 * discard output but never permits releasing its in-flight resources early. */
int vso_ticket_valid(VsoOutput*,uint64_t ticket);
/* Only after actual consumer fence, or before any submission on failure. */
int vso_finish(VsoOutput*,uint64_t ticket,int displayed);
int vso_rate(VsoOutput*,uint32_t rate,uint64_t now);
/* No clock/job/GPU work. Pending tickets retain everything until finish. */
int vso_invalidate(VsoOutput*);
int vso_destroy(VsoOutput**);
#endif
