/* Menu Font2dStandardShader only: preserve glyph proportions and anchors.
 * Original setup13B840 forwards its model to renderer137B80 at13B87B.
 * Own work: djultra64. Original assets, metrics and shader remain intact. */
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
extern _Thread_local uint32_t g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void __real_sub_0013B840(void);
static int range(uint32_t p,uint32_t n)
{return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static int read_words(uint32_t p,void*out,uint32_t n)
{if(!range(p,n))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return (uint64_t)a+an>b&&(uint64_t)b+bn>a;}
static int selected_model(uint32_t *matrix,float value[16])
{
 uint32_t dimensions[2],args[3],shader,renderer,renderer_class,manager,canvas[2];
 float projection[16];
 const char*gate=getenv("TOPSPIN_RUI_FONT_ASPECT");
 if((gate&&strcmp(gate,"1"))||!topspin_native_video_selected_size("MENU",dimensions)
    ||dimensions[0]!=1280u||dimensions[1]!=720u
    ||!read_words(g_esp,args,sizeof args)||!read_words(g_ecx,&shader,4)
    ||shader!=0x1fc898u||!read_words(0x2b9694u,&renderer,4)
    ||!read_words(renderer,&renderer_class,4)||renderer_class!=0x1fc7b0u
    ||!read_words(0x2b969cu,&manager,4)||!range(manager,0x145cu)
    ||!read_words(manager+0x1420u,canvas,sizeof canvas)
    ||canvas[0]!=1280u||canvas[1]!=720u||!range(renderer,0xf64u)
    ||!read_words(renderer+0x90u,projection,sizeof projection)
    ||!read_words(args[2],value,64))return 0;
 /* This original input is borrowed, then restored after the shader copies it.
  * Reject aliasing with objects/ABI arguments which the original can mutate. */
 if(overlap(args[2],64,renderer,0xf64u)||overlap(args[2],64,manager,0x145cu)
    ||overlap(args[2],64,g_ecx,0x18u)||overlap(args[2],64,g_esp,12u))return 0;
 for(unsigned i=0;i<16;i++)if(!isfinite(value[i])||!isfinite(projection[i]))return 0;
 /* The proven Menu route is axis-aligned. Preserve rotated/sheared models
  * until their local basis and intended placement have separate evidence. */
 const unsigned zero[]={1,2,3,4,6,7,8,9,11};
 for(unsigned i=0;i<sizeof zero/sizeof zero[0];i++)if(fabsf(value[zero[i]])>0.00001f)return 0;
 if(value[0]<=0.f||value[5]<=0.f||value[10]<=0.f||value[15]!=1.f
    ||fabsf(value[0]/value[5]-4.f/3.f)>0.0001f
    ||projection[0]<=0.f||projection[5]<=0.f
    ||fabsf(projection[0]/projection[5]-9.f/16.f)>0.0001f)return 0;
 *matrix=args[2];return 1;
}
void __wrap_sub_0013B840(void)
{
 uint32_t matrix=0;float model[16];
 int applied=selected_model(&matrix,model);
 if(applied){float x=model[0]*.75f;memcpy((void*)((uintptr_t)g_xbox_mem_offset+matrix),&x,4);}
 __real_sub_0013B840();
 if(applied){
  static unsigned logs;
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+matrix),model,4);
  const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
  if(trace&&!strcmp(trace,"1")&&logs++<4)
   fprintf(stderr,"[MENU-RUI-FONT-ASPECT] original_scale=%g/%g requested_scale=%g/%g anchors_preserved=1\n",
    (double)model[0],(double)model[5],(double)(model[0]*.75f),(double)model[5]);
 }
}
