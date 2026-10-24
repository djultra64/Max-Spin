/* Per-image original Font2d model contract. Own work: djultra64. */
#ifndef TOPSPIN_RUI_FONT_ASPECT_H
#define TOPSPIN_RUI_FONT_ASPECT_H
#include <stdint.h>
typedef struct TopspinRuiFontProfile {
 const char *label;
 uint32_t font_class,renderer_class,renderer_global,manager_global;
} TopspinRuiFontProfile;
typedef int (*TopspinFontVideoSelectedSize)(const char *,uint32_t[2]);
void topspin_rui_font_aspect(const TopspinRuiFontProfile *,TopspinFontVideoSelectedSize,void(*original)(void));
#endif
