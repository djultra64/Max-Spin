/* Exact resolved native draw state, process-local immutable handles. djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_GPU_DRAW_H
#define TOPSPIN_VISUAL_SCENE_GPU_DRAW_H
#include <stdint.h>
#define D3D8_SCENE_GPU_DRAW_VERSION 1u
#define D3D8_SCENE_PS_MAX 1024u
typedef struct {
 uint32_t version,bytes,ps_bytes,depth_format;
 uint64_t device_epoch,pipeline,pipeline_layout,descriptor_layout,samplers[4];
 unsigned char pipeline_key[32];
 uint32_t vertex_shader,pixel_shader,layout,instanced,topology,color_format,has_depth,stencil_enabled;
 uint32_t stencil_reference,stencil_compare_mask,stencil_write_mask,cube_mask;
 float depth_bias[3],blend[4],frame_constants[12];
 unsigned char ps_constants[D3D8_SCENE_PS_MAX];
} D3D8SceneGpuDraw;
/* POST successful original native draw only. Copies resolved cache entries and
 * already-written UBO bytes; no bind_draw/combiner/shader/compiler/GPU calls.
 * Pair with view_state and actual geometry/program/resources from that draw.
 * No guest pointer or descriptor-set/UBO ring address is retained. */
int d3d8_vk_scene_resolved_draw(void*out,uint32_t capacity,uint32_t*bytes);
/* Real executor/scene/device required. May validate after original frame closed.
 * Handles are borrowed from immutable process-lifetime original caches, not
 * arbitrary pointers or owned resources. Root must keep device/caches alive. */
int d3d8_vk_scene_resolved_draw_validate(const D3D8SceneGpuDraw*);
#endif
