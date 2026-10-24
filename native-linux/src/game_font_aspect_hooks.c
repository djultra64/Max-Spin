/* Generated image-scoped Font2d adapter. Own work: djultra64. */
#include "native_video.h"
#include "rui_canvas.h"
#include "rui_font_profiles.h"
extern void __real_sub_0012BCA0(void);
static const TopspinRuiFontProfile profile=TOPSPIN_RUI_FONT_GAME_PROFILE;
void __wrap_sub_0012BCA0(void)
{topspin_rui_font_aspect(&profile,topspin_game_video_selected_size,__real_sub_0012BCA0);}
