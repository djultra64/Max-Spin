/* Guest render-target routing. Own integration: djultra64.
 * Addresses are resolved guest allocation addresses, not DMA offsets.
 * Supports RGB565, AA0 RGBA and the observed 0x0113/color=0 depth-only target.
 * Sampled D16 aliases are supported. RECOMP_GUEST_AA1=1 opts into regular
 * horizontal2x AA1 backing; exact NV2A raster sample locations are unverified. */
#ifndef D3D8_VK_RT_H
#define D3D8_VK_RT_H
#include <stdint.h>
/* width/height are logical. Experimental1128 backing/alias width is2x;
 * color/zeta pitches describe physical rows. The format word retains AA bits. */
typedef struct {
    uint32_t color,zeta,width,height,color_pitch,zeta_pitch,format;
    uint32_t clip_x,clip_y,clip_width,clip_height;
    uint32_t extent_is_minimum;
} D3D8VkTarget;
/* Startup-only added raster density. Guest coordinates, pitches and sampled
 * texture dimensions stay unchanged. Ratios1..3 inclusive are supported;
 * invalid values or an existing device return0 without changing configuration.
 * Explicit density also sets the host-main raster, overriding legacy host-only
 * integer scaling. Apply requires a process/device restart. djultra64. */
int d3d8_vk_set_guest_raster_scale(uint32_t numerator,uint32_t denominator);
typedef struct {uint32_t guest_width,guest_height,raster_width,raster_height;} D3D8VkRasterExtent;
/* Pure metadata query; output is unchanged on failure. */
int d3d8_vk_rt_extent(uint32_t color,D3D8VkRasterExtent *extent);
/* Actual chosen device framebuffer/image limits, not a VRAM or performance
 * guarantee. Returns0 until a device is ready; output unchanged on failure. */
int d3d8_vk_raster_limits(uint32_t *width,uint32_t *height);
/* 1 selected, 0 unsupported (state unchanged), -1 invalid/resource failure.
 * NULL explicitly restores the existing main target, leaving its AA/extent
 * policy unchanged. Caller must never silently draw unsupported offscreen
 * work into the main target. No guest target is selected by default. */
int d3d8_vk_rt_select(const D3D8VkTarget *target);
/* 1 alias bound, 0 not an existing target, -1 invalid/feedback loop.
 * Call before uploading CPU bytes; a GPU-produced target owns its contents.
 * D16/LIN_D16 resolves by zeta address and binds the live depth-only sampled
 * view; sampling the currently active zeta is rejected as feedback.
 * RGBA aliases support LIN_A8R8G8B8 texel coordinates and normalized
 * A8R8G8B8 through independent stable metadata views of the same image.
 * Caller still supplies the guest's texture addressing/filter state. */
int d3d8_vk_rt_bind(unsigned stage,uint32_t color,uint32_t width,uint32_t height,uint32_t texture_format);
/* Diagnostic synchronous RGBA8 readback in guest storage/alias dimensions;
 * added density is downsampled, preserving callers' existing buffer size.
 * not used for normal texture binds. */
int d3d8_vk_rt_readback(uint32_t color,void *rgba,uint32_t pitch);
/* Select a known RGBA allocation for host Presents, independent of the
 * current draw target. Address0 restores the host default. Returns1 accepted,
 * 0 unknown/unsupported (selection unchanged), -1 invalid/not initialized. */
int d3d8_vk_rt_present(uint32_t color);
/* NV097 polygon-offset factor and bias in guest zeta units. The backend
 * converts Z16 units when the active host attachment uses D24 precision. */
void d3d8_vk_set_polygon_offset(int enabled,float factor,float units);
#endif
