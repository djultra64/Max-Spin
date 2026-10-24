/* Owned symbol projection onto immutable original RUI resource copies.
 * Existing texture decoding/swizzling is included from its credited SDK.
 * Own resource/parser/rasterization work: djultra64. */
#include "controller_rui_resources.h"
#include "input/input_identity.h"
#include "d3d/d3d8_swizzle.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t*p){uint32_t x;memcpy(&x,p,4);return x;}
static uint16_t half(const uint8_t*p){uint16_t x;memcpy(&x,p,2);return x;}
static int power2(uint32_t x){return x&&!(x&(x-1))&&x<=2048;}
static int coordinate(const uint8_t*p,uint32_t extent,uint32_t*out){
 float f;memcpy(&f,p,4);if(!isfinite(f)||f<0||f>1)return 0;
 float pixel=f*extent;long v=lroundf(pixel);if(fabsf(pixel-(float)v)>.001f)return 0;*out=(uint32_t)v;return 1;
}
int topspin_rui_font_layout(const void*data,size_t bytes,TopSpinRuiLayout*out){
 if(!data||!out||bytes<28)return 0;
 const uint8_t*b=data;TopSpinRuiLayout l={0};
 if(word(b)!=6||!power2(l.width=word(b+8))||!power2(l.height=word(b+12)))return 0;
 uint32_t ranges=word(b+20);if(!ranges||ranges>1024||bytes<28+(size_t)ranges*4)return 0;
 size_t records=28+(size_t)ranges*4;uint32_t count=word(b+records-4);
 if(count>65536||bytes!=records+(size_t)count*28)return 0;
 uint32_t offset=0,previous=0,seen=0;
 for(uint32_t n=0;n<ranges;n++){
  uint32_t lo=half(b+24+n*4),hi=half(b+26+n*4);
  if(lo>hi||(n&&lo<=previous)||offset+hi-lo+1>count)return 0;
  for(unsigned face=0;face<9;face++){
   uint32_t code=face<6?0x2123+face:0x212A+face-6;if(code<lo||code>hi)continue;
   const uint8_t*g=b+records+(offset+code-lo)*28;TopSpinGlyphRect*r=face<4?&l.face[face]:face<6?&l.auxiliary[face-4]:&l.extra[face-6];
   if(!coordinate(g,l.width,&r->x0)||!coordinate(g+4,l.height,&r->y0)||
      !coordinate(g+8,l.width,&r->x1)||!coordinate(g+12,l.height,&r->y1)||
      r->x0>=r->x1||r->y0>=r->y1||r->x1-r->x0<5||r->y1-r->y0<7)return 0;
   seen|=1u<<face;
  }
  offset+=hi-lo+1;previous=hi;
 }
 if(offset!=count||(seen&15)!=15)return 0;
 l.auxiliary_mask=(seen>>4)&3;l.extra_mask=(seen>>6)&7;
 for(unsigned a=0;a<9;a++)for(unsigned c=0;c<a;c++){
  if(!(seen&(1u<<a))||!(seen&(1u<<c)))continue;
  TopSpinGlyphRect x=a<4?l.face[a]:a<6?l.auxiliary[a-4]:l.extra[a-6],y=c<4?l.face[c]:c<6?l.auxiliary[c-4]:l.extra[c-6];
  if(x.x0<y.x1&&y.x0<x.x1&&x.y0<y.y1&&y.y0<x.y1)return 0;
 }
 *out=l;return 1;
}
static int xpr_layout(const uint8_t*b,size_t bytes,uint32_t*w,uint32_t*h){
 if(!b||bytes<32||word(b)!=0x30525058||word(b+4)!=bytes||word(b+8)!=32||
    (word(b+12)&0x70000)!=0x40000||word(b+16)||word(b+20))return 0;
 uint32_t format=word(b+24);
 unsigned pixel_format=(format>>8)&255;
 if((pixel_format!=0x0e&&pixel_format!=0x0f&&pixel_format!=0x06)||((format>>16)&15)!=1||word(b+28))return 0;
 *w=1u<<((format>>20)&15);*h=1u<<((format>>24)&15);
 size_t payload=pixel_format==0x06?(size_t)*w**h*4:(size_t)((*w+3)/4)*((*h+3)/4)*16;
 return power2(*w)&&power2(*h)&&bytes==32+payload;
}
/* Original Xelu artwork, CC0; no procedural button lettering fallback. */
#include "controller_xelu_pixels.h"
static uint32_t sample(unsigned label,float x,float y){
 const uint32_t*p=topspin_xelu_pixels[label-1];
 float sx=x*64-.5f,sy=y*64-.5f;int ix=(int)floorf(sx),iy=(int)floorf(sy);
 float fx=sx-ix,fy=sy-iy,a=0,r=0,g=0,b=0;
 for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++){
  int xx=ix+dx,yy=iy+dy;if(xx<0||yy<0||xx>=64||yy>=64)continue;
  uint32_t c=p[yy*64+xx];float w=(dx?fx:1-fx)*(dy?fy:1-fy),alpha=(c>>24)*w;
  a+=alpha;r+=((c>>16)&255)*alpha;g+=((c>>8)&255)*alpha;b+=(c&255)*alpha;
 }
 if(a<.5f)return 0;
 return ((uint32_t)lroundf(a)<<24)|((uint32_t)lroundf(r/a)<<16)|((uint32_t)lroundf(g/a)<<8)|(uint32_t)lroundf(b/a);
}
static void paint(uint32_t*p,uint32_t width,TopSpinGlyphRect r,unsigned label){
 float w=(float)(r.x1-r.x0),h=(float)(r.y1-r.y0),side=fminf(w,h),ox=(w-side)*.5f,oy=(h-side)*.5f;
 for(uint32_t y=r.y0;y<r.y1;y++)for(uint32_t x=r.x0;x<r.x1;x++){
  float u=((x-r.x0)+.5f-ox)/side,v=((y-r.y0)+.5f-oy)/side;
  p[(size_t)y*width+x]=(u<0||u>=1||v<0||v>=1)?0:sample(label,u,v);
 }
}
static int project(const void*data,size_t bytes,const TopSpinRuiLayout*l,const TopSpinGlyphRect*rects,const uint8_t*labels,unsigned count,unsigned maximum,uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;
 if(!data||!l||!labels||!out||!out_bytes)return 0;
 for(unsigned i=0;i<count;i++)if(labels[i]<INPUT_LABEL_A||labels[i]>maximum)return 0;
 const uint8_t*b=data;uint32_t w,h;if(!xpr_layout(b,bytes,&w,&h)||w!=l->width||h!=l->height)return 0;
 size_t pixels=(size_t)w*h,total=32+pixels*4;
 uint32_t*rgba=malloc(pixels*4);uint8_t*owned=malloc(total);
 if(!rgba||!owned){free(rgba);free(owned);return 0;}
 if(((word(b+24)>>8)&255)==0x06)xbox_unswizzle_rect(rgba,b+32,w,h,4);
 else for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++)d3d8_dxt_decode_texel(b+32,(word(b+24)>>8)&255,x,y,w,&rgba[(size_t)y*w+x]);
 for(unsigned i=0;i<count;i++)paint(rgba,w,rects[i],labels[i]);
 memcpy(owned,b,32);uint32_t value=(uint32_t)total;memcpy(owned+4,&value,4);
 value=(word(b+24)&~0xff00u)|0x0600u;memcpy(owned+24,&value,4);
 xbox_swizzle_rect(owned+32,rgba,w,h,4);free(rgba);*out=owned;*out_bytes=total;return 1;
}
int topspin_rui_font_project(const void*abc,size_t abc_bytes,const void*xpr,size_t xpr_bytes,const uint8_t labels[4],uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;
 TopSpinRuiLayout l;if(!topspin_rui_font_layout(abc,abc_bytes,&l))return 0;
 return project(xpr,xpr_bytes,&l,l.face,labels,4,18,out,out_bytes);
}
int topspin_rui_sprite_project(const void*xpr,size_t bytes,unsigned label,uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;
 TopSpinRuiLayout l={0};if(!xpr_layout(xpr,bytes,&l.width,&l.height)||label>255)return 0;
 l.face[0]=(TopSpinGlyphRect){0,0,l.width,l.height};uint8_t id=(uint8_t)label;
 return project(xpr,bytes,&l,l.face,&id,1,18,out,out_bytes);
}

uint8_t topspin_rui_face_glyph(int family,unsigned label){
 if(family==INPUT_FAMILY_XBOX&&label>=INPUT_LABEL_A&&label<=INPUT_LABEL_Y)return (uint8_t)label;
 if(family==INPUT_FAMILY_PLAYSTATION&&label>=INPUT_LABEL_CROSS&&label<=INPUT_LABEL_TRIANGLE)return (uint8_t)label;
 if(family==INPUT_FAMILY_NINTENDO&&label>=INPUT_LABEL_A&&label<=INPUT_LABEL_Y)return (uint8_t)(TOPSPIN_GLYPH_SWITCH_A+label-INPUT_LABEL_A);
 return 0;
}
int topspin_rui_family_labels(int family,uint8_t labels[6]){
 if(!labels)return 0;
 uint8_t result[6];for(unsigned i=0;i<4;i++){result[i]=topspin_rui_face_glyph(family,input_family_face_label(family,i));if(!result[i])return 0;}
 result[4]=family==INPUT_FAMILY_XBOX?TOPSPIN_GLYPH_RB:family==INPUT_FAMILY_PLAYSTATION?TOPSPIN_GLYPH_R1:TOPSPIN_GLYPH_R;
 result[5]=family==INPUT_FAMILY_XBOX?TOPSPIN_GLYPH_LB:family==INPUT_FAMILY_PLAYSTATION?TOPSPIN_GLYPH_L1:TOPSPIN_GLYPH_L;
 memcpy(labels,result,6);return 1;
}
int topspin_rui_font_project_controls(const void*abc,size_t abc_bytes,const void*xpr,size_t xpr_bytes,const uint8_t labels[6],uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;
 TopSpinRuiLayout l;if(!topspin_rui_font_layout(abc,abc_bytes,&l)||l.auxiliary_mask!=3)return 0;
 TopSpinGlyphRect r[6];memcpy(r,l.face,sizeof l.face);memcpy(r+4,l.auxiliary,sizeof l.auxiliary);
 return project(xpr,xpr_bytes,&l,r,labels,6,18,out,out_bytes);
}

int topspin_rui_sprite_project9(const void*xpr,size_t bytes,unsigned label,uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;TopSpinRuiLayout l={0};
 if(!xpr_layout(xpr,bytes,&l.width,&l.height)||label>27)return 0;
 l.face[0]=(TopSpinGlyphRect){0,0,l.width,l.height};uint8_t id=(uint8_t)label;
 return project(xpr,bytes,&l,l.face,&id,1,27,out,out_bytes);
}
/* Extra order: right stick, Back, Start. Zero extras preserve legacy6 pixels. */
int topspin_rui_font_project_controls9(const void*abc,size_t abc_bytes,const void*xpr,size_t xpr_bytes,const uint8_t labels[9],uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;if(!labels)return 0;
 TopSpinRuiLayout l;if(!topspin_rui_font_layout(abc,abc_bytes,&l)||l.auxiliary_mask!=3)return 0;
 TopSpinGlyphRect r[9];uint8_t ids[9];unsigned n=6;
 memcpy(r,l.face,sizeof l.face);memcpy(r+4,l.auxiliary,sizeof l.auxiliary);memcpy(ids,labels,6);
 for(unsigned i=0;i<3;i++)if((l.extra_mask&(1u<<i))&&labels[6+i]){r[n]=l.extra[i];ids[n++]=labels[6+i];}
 return project(xpr,xpr_bytes,&l,r,ids,n,27,out,out_bytes);
}

/* Native HUD XPR1 entry, not an RUI font. Only the two baked button cells
 * change; localized text and every other decoded base-level texel stay exact.
 * Match/capture retains the complete original mip chain; owned HUD output has
 * a single explicit RGBA level, like other immutable prompt projections. */
int topspin_rui_hud_layout(const void*data,size_t bytes){
 const uint8_t*b=data;if(!b||bytes<32||word(b)!=0x30525058||word(b+4)!=bytes||word(b+8)!=32||
 (word(b+12)&0x70000u)!=0x40000u||word(b+16)||word(b+20)||word(b+28))return 0;
 uint32_t f=word(b+24);unsigned fmt=(f>>8)&255u,levels=(f>>16)&15u;
 if((fmt!=14&&fmt!=15)||((f>>20)&15u)!=7||((f>>24)&15u)!=6||!levels||levels>8)return 0;
 size_t n=32;for(unsigned l=0;l<levels;l++){unsigned w=128u>>l,h=64u>>l;if(!w)w=1;if(!h)h=1;n+=(size_t)((w+3)/4)*((h+3)/4)*16;}
 return n==bytes;
}
int topspin_rui_hud_project(const void*data,size_t bytes,const uint8_t labels[6],uint8_t**out,size_t*out_bytes){
 if(out)*out=NULL;if(out_bytes)*out_bytes=0;
 if(!labels||!out||!out_bytes||!topspin_rui_hud_layout(data,bytes))return 0;
 uint8_t*base=malloc(32+8192);if(!base)return 0;memcpy(base,data,32+8192);
 uint32_t value=32+8192;memcpy(base+4,&value,4);value=(word(base+24)&~0xf0000u)|0x10000u;memcpy(base+24,&value,4);
 TopSpinRuiLayout l={0};l.width=128;l.height=64;
 unsigned white_x=((word(base+24)>>8)&255u)==15u?94u:95u;
 TopSpinGlyphRect cells[2]={{103,0,128,33},{white_x,33,123,60}};uint8_t glyphs[2]={labels[4],labels[5]};
 int ok=project(base,32+8192,&l,cells,glyphs,2,27,out,out_bytes);free(base);return ok;
}
