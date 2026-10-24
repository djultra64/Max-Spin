/* Existing window/surface/swapchain only. Own work: djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_WSI_H
#define TOPSPIN_VISUAL_SCENE_WSI_H
#include <vulkan/vulkan.h>
#include <stdint.h>
#define VSP_WSI_IMAGES 8u
typedef struct {
 uint32_t version,bytes,width,height,format,count;uint64_t device_epoch,swap_epoch,token;
 VkSurfaceKHR surface;VkSwapchainKHR swap;VkImage images[VSP_WSI_IMAGES];
} VspWsiLease;
/* Configure before device/worker creation; OFF has no extra work. */
int d3d8_vk_scene_wsi_configure(void);
int d3d8_vk_scene_wsi_borrow(VspWsiLease*);
int d3d8_vk_scene_wsi_validate(const VspWsiLease*);
int d3d8_vk_scene_wsi_release(uint64_t token);
/* Nonblocking requests. retire returns1 only after ORIGINAL safe-boundary
 * maintenance actually retired the old swapchain; otherwise it retains token.
 * Caller must request retire only after semaphore drain/consumer fence SUCCESS. */
int d3d8_vk_scene_wsi_retire(uint64_t token);
int d3d8_vk_scene_wsi_refresh(void);
/* Original Present only: after frame_submit_wait and any original physical
 * QueuePresent. Uses existing swapchain_make, including its device idle wait.
 * NEVER invoke from nonblocking visual service. Keeps the same window. */
int d3d8_vk_scene_wsi_maintenance(void);
#endif
