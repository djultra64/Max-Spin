/* Original shared Menu preview node: bounded CPU-only state evidence.
 * MainMenu/TennisSchool/Environment use this class, not a screen-name offset.
 * Own work: djultra64. Diagnostics remain CPU-only; targeted geometry borrowing is delegated to menu_movie_layout. */
#include "menu_movie_layout.h"
#include "menu_movie_capture.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern _Thread_local uint32_t g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void __real_sub_000A67D0(void);
static int words(uint32_t p,uint32_t*out,unsigned n)
{if(p<0x10000u||(uint64_t)p+n*4u>0x4000000u)return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n*4u);return 1;}
struct State {
 uint32_t node,vptr,caller,video,video_info[7],matrix[16];
 uint32_t device,clip[6],surfaces[6],graphics,requested[6],aspect;
 int valid;
};
static void snapshot(struct State*s,uint32_t node,uint32_t stack)
{
 uint32_t args[2]={0},surface=0;
 memset(s,0,sizeof *s);s->node=node;
 s->valid=words(stack,args,2)&&words(node,&s->vptr,1)&&s->vptr==0x1ee1e0u
  &&(uint64_t)node+0xe0u<=0x4000000u&&words(node+0x34u,&s->video,1)
  &&words(s->video,s->video_info,7)&&words(args[1],s->matrix,16)
  &&words(0x166058u,&s->device,1)&&(uint64_t)s->device+0x1a0cu<=0x4000000u
  &&words(s->device+0xed0u,s->clip,6)&&words(s->device+0x19f4u,s->surfaces,6)
  &&words(0x3baa20u,&s->graphics,1)&&(uint64_t)s->graphics+0x198u<=0x4000000u
  &&words(s->graphics+0x17cu,s->requested,6)&&words(s->graphics+0x194u,&s->aspect,1);
 s->caller=args[0];
 if(s->valid){
  /* Same source selection as original155A97..155AB7. Keep its raw format/size
   * beside the viewport clamped by155AFD..155C50; do not assume requested=actual. */
  surface=s->surfaces[0]==s->surfaces[4]?s->surfaces[5]:s->surfaces[0];
  uint32_t desc[5]={0};if(!words(surface,desc,5))s->valid=0;
  else {s->surfaces[0]=surface;s->surfaces[1]=desc[3];s->surfaces[2]=desc[4];}
 }
}
void __wrap_sub_000A67D0(void)
{
 static struct State seen[16];static unsigned count;
 struct State before={0},after;uint32_t node=g_ecx,stack=g_esp;int report=0;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&count<16){
  snapshot(&before,node,stack);report=1;
  for(unsigned i=0;i<count;i++){
   /* Translation/animation do not consume the bounded slots. Preserve the
    * actual source-size and viewport facts which can distinguish a scene. */
   struct State key=before,old=seen[i];memset(key.matrix,0,sizeof key.matrix);memset(old.matrix,0,sizeof old.matrix);
   memset(key.video_info,0,5*sizeof(uint32_t));memset(old.video_info,0,5*sizeof(uint32_t));
   memset(key.surfaces+3,0,3*sizeof(uint32_t));memset(old.surfaces+3,0,3*sizeof(uint32_t));
   if(!memcmp(&key,&old,sizeof key)){report=0;break;}
  }
 }
 TopSpinMenuMovieBorrow borrow;topspin_menu_movie_layout_begin(node,stack,&borrow);
 __real_sub_000A67D0();
 topspin_menu_movie_layout_end(&borrow);
 topspin_menu_movie_capture_after(node,stack);
 if(report){
  snapshot(&after,node,stack);seen[count++]=before;
  fprintf(stderr,"[MENU-MOVIE-LAYOUT] node=%08x class=%08x caller=%08x valid=%d/%d video=%08x source=%ux%u device=%08x actual_viewport=%u,%u,%u,%u after=%u,%u,%u,%u requested=%u,%u,%u,%u aspect=%08x surface=%08x format=%08x size=%08x model_diag=%08x/%08x/%08x translation=%08x/%08x/%08x\n",
   before.node,before.vptr,before.caller,before.valid,after.valid,before.video,before.video_info[5],before.video_info[6],before.device,
   before.clip[0],before.clip[1],before.clip[2],before.clip[3],after.clip[0],after.clip[1],after.clip[2],after.clip[3],
   before.requested[0],before.requested[1],before.requested[2],before.requested[3],before.aspect,
   before.surfaces[0],before.surfaces[1],before.surfaces[2],before.matrix[0],before.matrix[5],before.matrix[10],before.matrix[12],before.matrix[13],before.matrix[14]);
 }
}
