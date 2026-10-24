/* Portable sprite-only Super-xBR CPU core. Own adapter: djultra64. */
#ifndef TOPSPIN_SPRITE_XBR_H
#define TOPSPIN_SPRITE_XBR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TOPSPIN_SPRITE_XBR_ALGORITHM 1u
#define TOPSPIN_SPRITE_XBR_MAX_EDGE 256u
#define TOPSPIN_SPRITE_XBR_WORK_LIMIT (64u*1024u*1024u)
/* Input must be an already decoded, private straight-RGBA8 region. Caller owns
 * its lifetime for this call; no GPU/guest pointers or texture eligibility are
 * inferred here. Clamp-to-edge within this region, no neighboring atlas data.
 * Optional cancellation is checked between rows/passes and before commit.
 * generation is an opaque caller cache token, copied unchanged to the result. */
typedef int (*TopspinSpriteXbrCancel)(void *user,uint64_t generation);
typedef struct TopspinSpriteXbrInput {
 const uint8_t *rgba;
 size_t rgba_bytes,pitch;
 uint32_t width,height,factor;
 uint64_t generation;
 size_t work_limit; /*0 selects64MiB; larger requests are rejected. */
 TopspinSpriteXbrCancel cancelled;
 void *user;
} TopspinSpriteXbrInput;
typedef struct TopspinSpriteXbrImage {
 uint8_t *rgba;
 size_t bytes,pitch;
 uint32_t width,height,factor,algorithm;
 uint64_t generation;
} TopspinSpriteXbrImage;
/* Initialize result to zero.1 commits an owned straight-RGBA8 image;0 rejects
 * invalid/budget/allocation failure;-1 is canceled. Failure leaves result
 * unchanged. A nonempty result is rejected; free it first. Source never changes.
 * MODE0,weight1,edge0.6,anti-ring1,float intermediates;4x repeats2x. Alpha is
 * independent coverage, not upstream's luminance scratch. Fully transparent
 * output may receive adjacent visible RGB for later straight-alpha sampling. */
int topspin_sprite_xbr_scale(const TopspinSpriteXbrInput*,TopspinSpriteXbrImage*);
void topspin_sprite_xbr_free(TopspinSpriteXbrImage*);
#ifdef __cplusplus
}
#endif
#endif
