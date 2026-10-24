/* Original environmentselect /Menu/video/ph_video only, native720p.
 * Original authored scale640x350 and decoded source640x350 become1280x525
 * under the RUI canvas'2x/1.5x basis. Fill that authored rectangle with
 * a centered vertical UV crop, preserving source aspect and all geometry.
 * Own integration: djultra64. Original assets remain immutable. */
#include "menu_movie_layout.h"
#include "native_video.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
extern ptrdiff_t g_xbox_mem_offset;
extern _Thread_local uint32_t g_esp;
extern void __real_sub_001613A0(void);
static _Thread_local TopSpinMenuMovieBorrow *movie_scope;
static int range(uint32_t p,size_t n){return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static int read_at(uint32_t p,void*out,size_t n){if(!range(p,n))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static int overlap(uint32_t a,size_t an,uint32_t b,size_t bn){return(uint64_t)a+an>b&&(uint64_t)b+bn>a;}
static int named(uint32_t node){
 uint32_t p,n;static const char expected[]="ph_video";
 /* Original2A8B6 reads Name, then2A8BC/2A770 assigns node+8's RichString. */
 return read_at(node+8,&p,4)&&p>=0x10010u&&read_at(p-12,&n,4)&&n==sizeof expected-1&&range(p,sizeof expected)&&!memcmp((void*)((uintptr_t)g_xbox_mem_offset+p),expected,sizeof expected);
}
int topspin_menu_movie_layout_begin(uint32_t node,uint32_t stack,TopSpinMenuMovieBorrow*b)
{
 if(!b)return 0;
 memset(b,0,sizeof *b);b->previous=movie_scope;movie_scope=NULL;
 const char*gate=getenv("TOPSPIN_ENV_MOVIE_LAYOUT");if(gate&&!strcmp(gate,"0"))return 0;
 uint32_t size[2],args[2],klass,video,info[7],manager,canvas[2],device,clip[4];float model[16];
 if(!topspin_native_video_selected_size("MENU",size)||size[0]!=1280||size[1]!=720
    ||!range(node,0xe0)||!read_at(stack,args,sizeof args)||args[0]!=0x28a3au
    ||!read_at(node,&klass,4)||klass!=0x1ee1e0u||!named(node)
    ||!read_at(node+0x34,&video,4)||!range(video,0x50)||!read_at(video,info,sizeof info)||info[5]!=640||info[6]!=350
    ||!read_at(0x2b969cu,&manager,4)||!range(manager,0x145cu)||!read_at(manager+0x1420u,canvas,sizeof canvas)||canvas[0]!=1280||canvas[1]!=720
    ||!read_at(0x166058u,&device,4)||!range(device,0x1a0cu)||!read_at(device+0xed0u,clip,sizeof clip)||clip[0]||clip[1]||clip[2]!=1280||clip[3]!=720
    ||!read_at(args[1],model,sizeof model))return 0;
 if(overlap(args[1],64,node,0xe0)||overlap(args[1],64,video,0x50)||overlap(args[1],64,manager,0x145c)||overlap(args[1],64,device,0x1a0c)||overlap(args[1],64,stack,8))return 0;
 for(unsigned i=0;i<16;i++)if(!isfinite(model[i]))return 0;
 const unsigned zero[]={1,2,3,4,6,7,8,9,11};
 for(unsigned i=0;i<sizeof zero/sizeof zero[0];i++)if(fabsf(model[zero[i]])>.00001f)return 0;
 if(fabsf(model[0]-1280.f)>.002f||fabsf(model[5]-525.f)>.002f||model[10]!=1.f||model[15]!=1.f)return 0;
 b->matrix=args[1];b->active=1;movie_scope=b;return 1;
}
void topspin_menu_movie_layout_end(TopSpinMenuMovieBorrow*b)
{
 if(!b)return;
 movie_scope=b->previous;
 if(!b->active)return;
 b->active=0;
 static unsigned reported;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&reported++<4)
  fprintf(stderr,"[MENU-ENV-MOVIE-LAYOUT] node_name=ph_video source=640x350 frame=1280x525 cover_v=.125..875 geometry_animation_preserved=1 input_restored=1\n");
}
void __wrap_sub_001613A0(void)
{
 uint32_t args[5]={0},sp=g_esp;float saved[20],cropped[20];int applied=0;
 /* OriginalCC030 owns a stack-local4vertexXYZUV array at entryESP+0x24.
  * Its centered unit quad and complete source UVs are required, so an
  * animated/partial-source/custom mesh cannot accidentally inherit COVER. */
 if(movie_scope&&movie_scope->active&&read_at(sp,args,sizeof args)
    &&args[0]==0xcc1adu&&args[1]==8&&args[2]==4&&args[4]==20
    &&(uint64_t)sp+0x24u==args[3]&&read_at(args[3],saved,sizeof saved)
    &&!overlap(args[3],sizeof saved,movie_scope->matrix,64)){
  static const float expected[20]={.5f,.5f,0,1,0,-.5f,.5f,0,0,0,-.5f,-.5f,0,0,1,.5f,-.5f,0,1,1};
  if(!memcmp(saved,expected,sizeof saved)){
   memcpy(cropped,saved,sizeof cropped);
   for(unsigned i=0;i<4;i++)cropped[i*5+4]=.125f+.75f*cropped[i*5+4];
   memcpy((void*)((uintptr_t)g_xbox_mem_offset+args[3]),cropped,sizeof cropped);applied=1;
  }
 }
 __real_sub_001613A0();
 if(applied)memcpy((void*)((uintptr_t)g_xbox_mem_offset+args[3]),saved,sizeof saved);
}
