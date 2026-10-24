/* Fresh Vulkan execution of owned visual scenes. Own work: djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_REPLAY_H
#define TOPSPIN_VISUAL_SCENE_REPLAY_H
#include <vulkan/vulkan.h>
#include "visual_scene_journal.h"
#include "visual_scene_gpu_draw.h"
#include "visual_vk_snapshot.h"
#define VRE_MAX_BYTES (64ull*1024*1024)
typedef struct VreContext VreContext;
typedef struct {
 void*user;int(*owner)(void*);int(*admitted)(void*,uint64_t device_epoch);
 /* Exact retained clone only, never mutable original storage. Must validate
  * resource/lease tuple, epoch and clone completion. No queues or guest work. */
 int(*describe)(void*,const VsjUse*,void*borrowed_lease,VksSource*);
 int(*material_valid)(void*,const D3D8SceneGpuDraw*);
 VkPhysicalDevice physical;VkDevice device;VkQueue queue;uint32_t family;
 uint64_t device_epoch,cpu_budget,gpu_budget;
} VreConfig;
typedef struct {
 uint64_t ticket;VkImage image;VkImageView view;VkImageLayout layout;
 VkFormat format;uint32_t width,height;VkFence fence;
} VreOutput;
enum {VRE_QUARANTINED=2,VRE_OK=1,VRE_WAIT=0,VRE_INVALID=-1,VRE_BUSY=-2,VRE_OWNER=-3,VRE_BUDGET=-4,VRE_GPU=-5,VRE_LOST=-6};
VreContext*vre_create(const VreConfig*);
/* Complete CPU preflight before any submit. A negative return means no new
 * submission/output. VRE_OK and VRE_QUARANTINED both publish a real ticket;
 * QUARANTINED means DEVICE_LOST with ambiguous submission and permanent retention.
 * After submission *out identifies the actual fence. Caller MUST hold frame and all
 * retained leases until vre_finish succeeds, even after timeout/lost/invalid
 * authority. Does not present or mutate original Vulkan globals/resources. */
int vre_submit(VreContext*,const VsjFrame*,VreOutput*out);
/* Only queries actual fence; timeout/error never destroys pending resources. Device loss permanently
 * quarantines the ticket, even if a later driver query would return success. */
int vre_poll(VreContext*,uint64_t ticket);
/* Only after the scene fence AND every subsequent output consumer have really
 * completed. A pending/lost fence rejects without freeing. No implicit wait. */
int vre_finish(VreContext*,uint64_t ticket);
/* Host leases prevent replay image release until independent consumers finish.
 * Acquire requires original replay fence observed complete; no GPU calls here.
 * Release only after consumer actual fence/WSI contract completed. */
int vre_output(VreContext*,uint64_t ticket,VreOutput*);
int vre_source_stamp(VreContext*,uint64_t ticket,VsjStamp*);
int vre_consumer_acquire(VreContext*,uint64_t ticket,uint64_t*owned_token);
int vre_consumer_release(VreContext*,uint64_t ticket,uint64_t owned_token);
int vre_destroy(VreContext**);
#endif
