/* Owned glyph-resource projection, original files remain immutable. djultra64. */
#ifndef TOPSPIN_CONTROLLER_RUI_RESOURCES_H
#define TOPSPIN_CONTROLLER_RUI_RESOURCES_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint32_t x0,y0,x1,y1;} TopSpinGlyphRect;
enum TopSpinRuiGlyph { TOPSPIN_GLYPH_LB=9,TOPSPIN_GLYPH_RB,TOPSPIN_GLYPH_L1,TOPSPIN_GLYPH_R1,TOPSPIN_GLYPH_L,TOPSPIN_GLYPH_R,TOPSPIN_GLYPH_SWITCH_A,TOPSPIN_GLYPH_SWITCH_B,TOPSPIN_GLYPH_SWITCH_X,TOPSPIN_GLYPH_SWITCH_Y };
typedef struct {uint32_t width,height;TopSpinGlyphRect face[4],auxiliary[2];unsigned auxiliary_mask;TopSpinGlyphRect extra[3];unsigned extra_mask;} TopSpinRuiLayout;
/* Original codepoint order: A/B/X/Y/Black/White. Auxiliary cells remain
 * absent unless actually present and bounded in the original ABC. */
uint8_t topspin_rui_face_glyph(int family,unsigned face_label);
int topspin_rui_family_labels(int family,uint8_t labels[6]);
int topspin_rui_font_project_controls9(const void*,size_t,const void*,size_t,const uint8_t labels[9],uint8_t**,size_t*);
int topspin_rui_font_project_controls(const void*,size_t,const void*,size_t,const uint8_t labels[6],uint8_t**,size_t*);
int topspin_rui_font_layout(const void *abc,size_t bytes,TopSpinRuiLayout *layout);
int topspin_rui_font_project(const void *abc,size_t abc_bytes,const void *xpr,size_t xpr_bytes,
                            const uint8_t labels[4],uint8_t **owned_xpr,size_t *owned_bytes);
int topspin_rui_sprite_project9(const void*,size_t,unsigned,uint8_t**,size_t*);
int topspin_rui_sprite_project(const void *xpr,size_t xpr_bytes,unsigned label,
                              uint8_t **owned_xpr,size_t *owned_bytes);
/* Exact native Attitude HUD dimensions; original source chain is immutable. */
int topspin_rui_hud_layout(const void*,size_t);
int topspin_rui_hud_project(const void*,size_t,const uint8_t[6],uint8_t**,size_t*);
#endif
