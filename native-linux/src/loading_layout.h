/* Exact original loading-image geometry. Own work: djultra64. */
#ifndef TOPSPIN_LOADING_LAYOUT_H
#define TOPSPIN_LOADING_LAYOUT_H
#include <stdint.h>
typedef struct TopspinLoadingVertex {
 float x,y,z,rhw;uint32_t color;float u,v;
} TopspinLoadingVertex;
/* Four original FVF144 vertices, unchanged fan topology and allocation.
 * Real content is640x480, optionally in1024x512 POT backing. */
int topspin_loading_build_stretched(const TopspinLoadingVertex original[4],uint32_t backing_width,uint32_t backing_height,TopspinLoadingVertex output[4]);
#endif
