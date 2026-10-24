/* Owned display policy, no native window/swapchain handles. djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_HOST_POLICY_H
#define TOPSPIN_VISUAL_SCENE_HOST_POLICY_H
#include <stdint.h>
typedef struct {
 uint32_t version,bytes,scaling,keep_aspect,linear,vsync;
 uint32_t aspect[2],canvas[2],render_extent[2],swap_extent[2];
 int32_t source[6],destination[6];uint32_t filter,gamma_enabled;
 float gamma[256][4];
} D3D8SceneHostPolicy;
/* Actual GPU executor only, no flush/clock/guest reads or submissions.
 * Call at original host blit, before resize/out-of-date handling changes it.
 * This is a policy snapshot, not permission to reuse original swapchain images.
 * Consumer must acquire its own current image and recompute for a changed
 * swap extent; an incompatible policy invalidates the old requested output. */
int d3d8_vk_scene_host_policy(D3D8SceneHostPolicy*);
#endif
