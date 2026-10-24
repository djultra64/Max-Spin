/* Original RUI resize arguments, scoped to a proven image initializer.
 * Own work: djultra64. No viewport/global graphics overrides. */
#include "rui_canvas.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
static int words(uint32_t va,uint32_t*out,size_t n)
{
 if(va<0x10000u||n>0x4000000u/4u||(uint64_t)va+n*4u>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),n*4u);return 1;
}
void topspin_rui_canvas_resize(const TopspinRuiCanvasProfile*p,TopspinVideoSelectedSize selected,void(*original)(void))
{
 uint32_t args[4],manager=0,size[2],context=g_ecx;int applied=0;
 if(selected(p->label,size)&&size[0]==1280&&size[1]==720
    &&words(g_esp,args,4)&&args[0]==p->initial_return
    &&args[1]==640&&args[2]==480&&args[3]==0
    &&context>=0x10000u&&(uint64_t)context+0x1458u<=0x4000000u
    &&words(p->manager_global,&manager,1)&&manager==context){
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+g_esp+4),size,sizeof size);applied=1;
 }
 original();
 static unsigned reported;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(applied&&trace&&!strcmp(trace,"1")&&reported<4){
  uint32_t canvas[2]={0};int valid=words(context+0x1420u,canvas,2);++reported;
  fprintf(stderr,"[%s-RUI-CANVAS] caller=%08x manager=%08x original=640x480 requested=%ux%u original_overscan=0 result_valid=%d canvas=%ux%u\n",
          p->label,args[0],context,size[0],size[1],valid,canvas[0],canvas[1]);
 }
}
