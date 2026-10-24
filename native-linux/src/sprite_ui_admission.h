/* Whole independent UI XPR admission; own work djultra64. */
#ifndef TOPSPIN_SPRITE_UI_ADMISSION_H
#define TOPSPIN_SPRITE_UI_ADMISSION_H
#include <stddef.h>
#include <stdint.h>
typedef const void*(*SxUiMap)(void*,uint32_t,size_t);
/* Host init before guest/GPU threads. Initial scope is Menu ONLY, verified
 * RRTexture class1FC6FC,3 exact source assets in immutable private cache.
 * No guessed equivalent VAs for other images. Failure leaves registry OFF. */
int sxui_configure(const char*private_cache,const char*image,SxUiMap,void*);
/* After successful original RRTexture load / before original retire. These
 * observe original guest data; no pointer/header/encoded byte writes. */
int sxui_available(void);
int topspin_sprite_ui_initialize(const char*image);
int topspin_sprite_ui_register_backend(void);
void sxui_loaded(uint32_t object);
void sxui_retire(uint32_t object);
/* GPU ready-texture callback; original encoded bytes MUST be readable/stable
 * for span (already validated by ready_tex_fill). Must call on EVERY bind,
 * including cache hits. levels!=1/cube/RT alias never invoke this path.
 * Returns canonical profile1..3 or0 => Original. Rechecks all encoded bytes,
 * so old admission cannot authorize later mutations at same guest address. */
uint32_t sxui_match(uint32_t guest_data,uint32_t format,uint32_t width,uint32_t height,uint32_t levels,const void*encoded,size_t span);
#endif
