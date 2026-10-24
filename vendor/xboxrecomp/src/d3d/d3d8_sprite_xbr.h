/* Linux Vulkan backend filter contract. Proposed; djultra64. */
#ifndef TOPSPIN_D3D8_SPRITE_XBR_H
#define TOPSPIN_D3D8_SPRITE_XBR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Pre-device startup only.1 Original,2/4 Super-xBR. No timing changes. */
int d3d8_vk_set_sprite_filter(unsigned factor);
/* Runtime format/queue/owner capability, not an assertion that every sprite
 * is admitted. Only exact independent UI profiles can produce filteredviews. */
int d3d8_vk_sprite_filter_supported(void);
unsigned d3d8_vk_sprite_filter_requested(void); /* startup only, no GPU query */
unsigned d3d8_vk_sprite_filter_current(void);
/* Called with original texture COM pointer and native authenticated profile
 * immediately before SetTexture, including all ready cache hits.0 clears.
 * Does not change COM identity, headers, guest metadata, sampler or UVs. */
void d3d8_vk_sprite_texture_admit(void *texture,uint32_t profile,unsigned uploaded_now);
/* After successful device idle and before device destruction.0 means pending
 * resources remain quarantined: keep context/device alive, never destroy them. */
int d3d8_vk_sprite_filter_close_idle(void);
#ifdef __cplusplus
}
#endif
#endif
