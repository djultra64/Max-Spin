/* Explicit same-window independent output consumer; djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_PRESENT_H
#define TOPSPIN_VISUAL_SCENE_PRESENT_H
#include "visual_scene_replay.h"
#include "visual_scene_recorder.h"
#include "visual_scene_wsi.h"
typedef struct VspContext VspContext;
typedef struct {
 void*user;int(*owner)(void*);int(*admitted)(void*,uint64_t);
 int(*wsi_borrow)(void*,VspWsiLease*);int(*wsi_valid)(void*,const VspWsiLease*);
 int(*wsi_release)(void*,uint64_t);int(*wsi_retire)(void*,uint64_t);int(*wsi_refresh)(void*);
 VkPhysicalDevice physical;VkDevice device;VkQueue queue;uint32_t family;uint64_t device_epoch,gpu_budget;
} VspConfig;
typedef struct {uint64_t ticket,source_ticket;VkFence fence;} VspOutput;
enum{VSP_QUARANTINED=2,VSP_OK=1,VSP_WAIT=0,VSP_INVALID=-1,VSP_BUSY=-2,VSP_OWNER=-3,VSP_GPU=-4,VSP_LOST=-5};
VspContext*vsp_create(const VspConfig*);
/* Actual source context/ticket required. Negative/WAIT means no consumer
 * ticket; OK/QUARANTINED publish one. QUARANTINED is permanent and may
 * precede submission if exact-token rollback failed; its fence is not proof
 * of GPU execution/completion. All source resources held by actual
 * vre_consumer token through consumer fence and WSI completion/retirement. */
int vsp_submit(VspContext*,VreContext*,const VreOutput*,const VsrHostPolicy*,VspOutput*);
/* Actual SDK111 SOURCE_ONLY receipt, copied as pure requested policy + source
 * and session id. No conversion to fabricated legacy raw WSI receipts.
 * Session authority comes from the real SDK117 gate, rechecked before display. */
int vsp_submit_source_only(VspContext*,VreContext*,const VreOutput*,const VsrSourceOnlyPolicy*,VspOutput*);
/* Nonblocking actual fence. Presents exactly once after success and current
 * authority; never fabricates display completion from a frame counter. */
int vsp_poll(VspContext*,uint64_t ticket);
int vsp_finish(VspContext*,uint64_t ticket);
int vsp_destroy(VspContext**);
#endif
