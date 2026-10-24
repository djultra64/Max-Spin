/* Executor-owned real Vulkan scene resources. Own work: djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_PROVIDER_H
#define TOPSPIN_VISUAL_SCENE_PROVIDER_H
#include "visual_frame_packet.h"
/* Configure snapshot executor ownership before device creation. This API does
 * not discover the environment, start workers or install journal callbacks. */
typedef int(*D3D8SceneAdmission)(void*);
/* The admission guard reads only owned host atomics and revokes old authority
 * immediately on destructor/reuse or a timed-out publication job. Called on
 * the GPU executor: no guest reads, allocation, jobs or reentry permitted. */
int d3d8_vk_scene_configure(D3D8SceneAdmission,void*);
/* On the actual GPU executor, publish constructor-authoritative scene/view IDs
 * carried by an ordered job before the original render. Neither original frame
 * serial nor sample index is a continuity ID. Zero invalidates old leases.
 * Caller must not pass a guessed guest address or a constant generation. */
int d3d8_vk_scene_continuity(VfpGeneration);
int d3d8_vk_scene_provider(VfpProvider*,uint64_t *actual_device_epoch);
/* BEFORE original body reserves rings/binds pass: flush queued original work.
 * Must never be called from a raw Vulkan writer, POST, or mid-upload. */
int d3d8_vk_scene_prepare(void);
/* Non-submitting bindings. flags: bit0=color target, bit1=depth target,
 * bit2=the four exact sampled images. Includes depth-only target support.
 * Caller copies metadata before return; resources need provider retention. */
int d3d8_vk_scene_bindings(unsigned flags,VfpResource*,unsigned capacity,unsigned *count);
/* Exact original upload image, before/after its writer; whole storage/mips.
 * No flush and no retaining from this call. PRE outer Unlock must flush first. */
int d3d8_vk_scene_upload_resource(uint64_t image,VfpResource*);
typedef struct {
 uint32_t version,bytes,depth_only,reserved;
 uint32_t logical[2],physical[2],backbuffer_logical[2],backbuffer_physical[2];
 uint32_t guest_scissor[4];int32_t scissor_xy[2];uint32_t scissor_wh[2];
 float viewport[6],canvas_scale[2],placement[2];
} D3D8SceneViewState;
/* Actual completed selection, AFTER successful rt_present; never currentRT.
 * Zero selects the real default vk.color only while rt_presented is NULL. */
int d3d8_vk_scene_completed_resource(uint32_t actual_guest_color,VfpResource*);
/* Pointer-free exact logical/raster/placement/scissor state, no flush/reads. */
int d3d8_vk_scene_view_state(D3D8SceneViewState*);
/* Requires actual consumer fences and all journal frame references released. */
int d3d8_vk_scene_close(void);
#endif
