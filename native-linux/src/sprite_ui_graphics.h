/* Native RUI graphics screen bridge. Own work: djultra64. */
#ifndef TOPSPIN_SPRITE_UI_GRAPHICS_H
#define TOPSPIN_SPRITE_UI_GRAPHICS_H
#include "graphics_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Enum mask for the native screen. Original is always selectable. The two
 * filters require both authenticated original resources and real Vk support.
 * A mask does not activate the renderer or create a GPU resource. */
uint32_t topspin_sprite_ui_filter_mask(void);
/* Running renderer mode, separate from the screen's requested next-session
 * setting. Unknown/unavailable states report Original. */
TopspinGraphicsFilter topspin_sprite_ui_filter_current(void);
/* Returns a stable ASCII label. Invalid enums return NULL. */
const char *topspin_sprite_ui_filter_label(TopspinGraphicsFilter);
#ifdef __cplusplus
}
#endif
#endif
