/* Protected loading-image geometry. Own work: djultra64. */
#ifndef TOPSPIN_LOADING_LAYOUT_H
#define TOPSPIN_LOADING_LAYOUT_H
#include <stdint.h>
typedef struct TopspinLoadingVertex {
 float x,y,z,rhw;uint32_t color;float u,v;
} TopspinLoadingVertex;
/* One original640x480 image -> uniform960x720 center and extended edge columns.
 * Output is three independent quads, ordered UL/UR/BR/BL, stride28. */
int topspin_loading_build_slices(const TopspinLoadingVertex original[4],TopspinLoadingVertex output[12]);
/* Exact LoadEng00.jpg content is640x480. D3DX_FILTER_NONE can place it
 * unscaled inside1024x512 POT backing. Validate its original reciprocal UVs
 * before excluding the unused padded texels. No texture/header is modified. */
int topspin_loading_build_backed_slices(const TopspinLoadingVertex original[4],uint32_t backing_width,uint32_t backing_height,TopspinLoadingVertex output[12]);
/* Last scoped Menu background draw actually used the proportional layout. */
int topspin_menu_loading_layout_applied(void);
#endif
