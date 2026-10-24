/* Native guest raster preferences. Own work: djultra64. */
#include "render_config.h"
#include "sprite_ui_admission.h"
#include "d3d/d3d8_sprite_xbr.h"
#include "d3d/d3d8_present.h"
#include "d3d/d3d8_vk_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned output_width=1280,output_height=720;
static TopspinGraphicsAA running_aa=TOPSPIN_AA_GUEST;
static int known_height(const char *text,unsigned *height)
{
 static const unsigned heights[]={720,900,1080,1440,1800,2160};
 if(!text)return 0;
 for(unsigned i=0;i<sizeof heights/sizeof heights[0];i++){
  char value[16];snprintf(value,sizeof value,"%u",heights[i]);
  if(!strcmp(text,value)){*height=heights[i];return 1;}
 }
 return 0;
}
static int implemented(const TopspinGraphicsSettings *s)
{return s && topspin_graphics_settings_validate(s,NULL,0) && (s->aa==TOPSPIN_AA_GUEST || (s->aa==TOPSPIN_AA_SSAA2X2 && s->resolution_height<=1080))
 && s->filter2d<TOPSPIN_FILTER_COUNT && s->frame_cap==60;}
int topspin_render_graphics_ready(const TopspinGraphicsSettings *s,void *unused)
{
 uint32_t w,h;(void)unused;
 if(!implemented(s)||!d3d8_vk_raster_limits(&w,&h))return 0;
 if(s->filter2d!=TOPSPIN_FILTER_ORIGINAL&&(!sxui_available()||!d3d8_vk_sprite_filter_supported()))return 0;
 /* Include the menu's guest AA1 horizontal backing in the size contract.
  * This does not promise all scene targets fit the separate RT byte budget. */
 return (uint64_t)topspin_graphics_resolution_width(s->resolution_height)*2u*(s->aa==TOPSPIN_AA_SSAA2X2?2u:1u)<=w
     && (uint64_t)s->resolution_height*(s->aa==TOPSPIN_AA_SSAA2X2?2u:1u)<=h;
}
TopspinGraphicsAA topspin_render_antialiasing(void){return running_aa;}
void topspin_render_output_size(unsigned *width,unsigned *height)
{if(width)*width=output_width;if(height)*height=output_height;}
int topspin_configure_render_scale(void)
{
 const char *session_height=getenv("TOPSPIN_SESSION_INTERNAL_HEIGHT");
 const char *explicit_height=getenv("TOPSPIN_INTERNAL_HEIGHT");
 const char *legacy=getenv("TOPSPIN_RENDER_SCALE");
 const char *preference_path=getenv("TOPSPIN_GRAPHICS_INI");
 const char *ini=getenv("TOPSPIN_SESSION_RENDER_INI");
 if(!ini)ini=preference_path;
 unsigned height=720,scale=1;int old_host_only=0,loaded_preferences=0;
 TopspinGraphicsSettings saved;topspin_graphics_settings_defaults(&saved);
 if(ini&&*ini){
  char error[256]={0};TopspinGraphicsSettings loaded;
  if(topspin_graphics_settings_load(ini,&loaded,error,sizeof error)
     && (loaded.aa==TOPSPIN_AA_GUEST||loaded.aa==TOPSPIN_AA_SSAA2X2)
     && loaded.filter2d<TOPSPIN_FILTER_COUNT&&loaded.frame_cap==60){saved=loaded;loaded_preferences=1;}
  else fprintf(stderr,"[RENDER-CONFIG] graphics.ini unavailable/unsupported; using720p (%s)\n",error[0]?error:"unimplemented preference");
 }
 height=saved.resolution_height;

 if(session_height){
  /* Immutable persistent-owner size. Menu preference commits affect the next
   * supervisor session, never a later child in the same visible window. */
  if(!known_height(session_height,&height)){
   fputs("[RENDER-CONFIG] invalid TOPSPIN_SESSION_INTERNAL_HEIGHT\n",stderr);return 0;
  }
 }else if(explicit_height){
  if(!known_height(explicit_height,&height)){
   fputs("[RENDER-CONFIG] invalid TOPSPIN_INTERNAL_HEIGHT: expected720,900,1080,1440,1800 or2160\n",stderr);return 0;
  }
 }else if(!loaded_preferences&&!(preference_path&&*preference_path)&&legacy){
  if(legacy[0]<'1'||legacy[0]>'4'||legacy[1]){
   fputs("[RENDER-CONFIG] invalid TOPSPIN_RENDER_SCALE: expected1..4\n",stderr);return 0;
  }
  scale=(unsigned)(legacy[0]-'0');old_host_only=1;
 }
 saved.resolution_height=height;
 if(!implemented(&saved)){
  fputs("[RENDER-CONFIG] SSAA2x2 requires nominal720p,900p or1080p; requested combination unsupported\n",stderr);return 0;
 }
 if(!d3d8_vk_set_sprite_filter(saved.filter2d==TOPSPIN_FILTER_SUPERXBR4X?4:saved.filter2d==TOPSPIN_FILTER_SUPERXBR2X?2:1))return 0;
 unsigned multiplier=saved.aa==TOPSPIN_AA_SSAA2X2?2u:1u;
 if(!old_host_only && !d3d8_vk_set_guest_raster_scale(height*multiplier,720)){
  fputs("[RENDER-CONFIG] raster configuration rejected; device must not exist yet\n",stderr);return 0;
 }
 if(setenv("RECOMP_VIDEO_WIDESCREEN","1",1)||setenv("RECOMP_VIDEO_MODE","720p",1)){
  perror("[RENDER-CONFIG] video settings");return 0;
 }
 d3d8_present_enable_scaling(1);d3d8_present_set_render_scale(scale);
 d3d8_present_set_canvas_aspect(0,0);d3d8_present_set_aspect(1,16,9);
 /* Preserve AF quality default; explicit diagnostic0 remains available. */
 if(!getenv("RECOMP_MAX_ANISOTROPY")&&setenv("RECOMP_MAX_ANISOTROPY","1",1)){
  perror("[RENDER-CONFIG] texture quality");return 0;
 }
 output_width=old_host_only?1280u*scale:height/9u*16u;
 output_height=old_host_only?720u*scale:height;
 running_aa=saved.aa;
 fprintf(stderr,"[RENDER-CONFIG] AA=%s; actual scene raster %ux%u before presentation resolve\n",running_aa==TOPSPIN_AA_SSAA2X2?"SSAA2x2":"guest",output_width*multiplier,output_height*multiplier);
 fprintf(stderr,"[RENDER-CONFIG] title720p widescreen; requested internal/output %ux%u; %s; requires process restart\n",output_width,output_height,old_host_only?"legacy host-only scale, guest RT unchanged":"real guest RGBA raster density");
 return 1;
}
