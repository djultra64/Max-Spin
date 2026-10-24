/* Primary RUI sizing contract. Own work: djultra64. */
#ifndef TOPSPIN_RUI_CANVAS_H
#define TOPSPIN_RUI_CANVAS_H
#include <stdint.h>
typedef struct TopspinRuiCanvasProfile {
    const char *label;uint32_t manager_global,initial_return;
} TopspinRuiCanvasProfile;
typedef int (*TopspinVideoSelectedSize)(const char*,uint32_t[2]);
void topspin_rui_canvas_resize(const TopspinRuiCanvasProfile*,TopspinVideoSelectedSize,void(*original)(void));
int topspin_game_video_selected_size(const char*,uint32_t[2]);
int topspin_selector_video_selected_size(const char*,uint32_t[2]);
#endif
