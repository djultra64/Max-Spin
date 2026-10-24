/* No overlay or SDL dependency: original RUI screen consumes this contract.
 * Own work: djultra64. */
#include "sprite_ui_graphics.h"
#include "sprite_ui_admission.h"
#include "d3d/d3d8_sprite_xbr.h"
#include <stddef.h>
uint32_t topspin_sprite_ui_filter_mask(void)
{
 uint32_t mask=1u<<TOPSPIN_FILTER_ORIGINAL;
 if(sxui_available()&&d3d8_vk_sprite_filter_supported())
  mask|=(1u<<TOPSPIN_FILTER_SUPERXBR2X)|(1u<<TOPSPIN_FILTER_SUPERXBR4X);
 return mask;
}
TopspinGraphicsFilter topspin_sprite_ui_filter_current(void)
{
 switch(d3d8_vk_sprite_filter_current()){
 case 2:return TOPSPIN_FILTER_SUPERXBR2X;
 case 4:return TOPSPIN_FILTER_SUPERXBR4X;
 default:return TOPSPIN_FILTER_ORIGINAL;
 }
}
const char *topspin_sprite_ui_filter_label(TopspinGraphicsFilter filter)
{
 switch(filter){
 case TOPSPIN_FILTER_ORIGINAL:return "ORIGINAL";
 case TOPSPIN_FILTER_SUPERXBR2X:return "SUPER-XBR 2X";
 case TOPSPIN_FILTER_SUPERXBR4X:return "SUPER-XBR 4X";
 default:return NULL;
 }
}
