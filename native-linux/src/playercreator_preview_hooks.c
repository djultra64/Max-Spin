/* Scale the original Creator model node's authored viewport into native720.
 * Original89090 sets projection, draws once and restores the previous viewport.
 * Preserve that transaction and its authored node data. Own work: djultra64. */
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_00089090_gen(void);
static int words(uint32_t va,uint32_t*out,size_t n)
{
 if(va<0x10000u||(uint64_t)va+4u*n>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),4u*n);return 1;
}
void sub_00089090(void)
{
 uint32_t node=g_ecx,caller=0,table=0,model=0,size[2],v[6];int applied=0;
 if(topspin_native_video_selected_size("PLAYERCREATOR",size)
    &&size[0]==1280&&size[1]==720
    &&words(g_esp,&caller,1)&&caller==0x000892b4u
    &&node>=0x10000u&&(uint64_t)node+0xedu<=0x4000000u
    &&words(node,&table,1)&&table==0x001e8510u
    &&words(node+0x34,&model,1)&&model>=0x10000u&&model<0x4000000u
    &&words(node+0xd0,v,6)
    &&v[0]==320&&v[1]==0&&v[2]==320&&v[3]==480
    &&v[4]==0&&v[5]==0x3f800000u){
  const uint32_t scaled[4]={640,0,640,720};
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+node+0xd0),scaled,sizeof scaled);
  applied=1;
 }
 sub_00089090_gen();
 if(applied){
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+node+0xd0),v,4u*sizeof(uint32_t));
  static unsigned reported;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
  if(trace&&!strcmp(trace,"1")&&reported++<2)
   fputs("[CREATOR-PREVIEW] authored320,0,320,480 rendered640,0,640,720; original projection/draw/restore retained\n",stderr);
 }
}
