/* Generated image-scoped Font2d adapter. Own work: djultra64. */
#include "native_video.h"
#include "rui_canvas.h"
#include "rui_font_profiles.h"
extern void __real_sub_00076110(void);
static const TopspinRuiFontProfile profile=TOPSPIN_RUI_FONT_SHOP_PROFILE;
void __wrap_sub_00076110(void)
{topspin_rui_font_aspect(&profile,topspin_native_video_selected_size,__real_sub_00076110);}
