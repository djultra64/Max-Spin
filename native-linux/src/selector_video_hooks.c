/* Scoped Training/Online original video selector request. Own work: djultra64.
 * Explicit image profile only; no Game or Menu framework mode substitution. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "selector_video_profiles.h"
extern _Thread_local uint32_t g_esp,g_ecx,g_eax;
extern ptrdiff_t g_xbox_mem_offset;
extern void TOPSPIN_SELECTOR_REAL(void);
static uint32_t selected_context,selected_camera;
static int words(uint32_t va,uint32_t*out,size_t n)
{
 if(va<0x10000u||n>0x4000000u/4u||(uint64_t)va+n*4u>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),n*4u);return 1;
}
void TOPSPIN_SELECTOR_WRAP(void)
{
 const char *wide=getenv("RECOMP_VIDEO_WIDESCREEN"),*mode=getenv("RECOMP_VIDEO_MODE"),*off=getenv("TOPSPIN_NATIVE720");
 uint32_t args[2],record[4],context=g_ecx;int applied=0;
 if(wide&&!strcmp(wide,"1")&&mode&&!strcmp(mode,"720p")&&!(off&&!strcmp(off,"0"))
    &&context>=0x10000u&&(uint64_t)context+0x44u<=0x4000000u
    &&words(g_esp,args,2)&&args[0]==TOPSPIN_SELECTOR_RETURN&&args[1]==2
    &&words(TOPSPIN_SELECTOR_RECORD8,record,4)
    &&record[0]==1280&&record[1]==720&&record[2]==1&&record[3]==1){
  const uint32_t request=8;memcpy((void*)((uintptr_t)g_xbox_mem_offset+g_esp+4),&request,4);applied=1;
 }
 /* This executes the title's real capability validation, original Reset,
  * camera projection and failure/fallback loop. No HRESULT is substituted. */
 TOPSPIN_SELECTOR_REAL();
 if(applied){
  uint32_t size[2],camera=0,aspect=0;selected_context=selected_camera=0;
  if(words(context+4u,size,2)&&size[0]==1280&&size[1]==720
     &&words(TOPSPIN_SELECTOR_CAMERA,&camera,1)&&camera>=0x10000u
     &&(uint64_t)camera+0x198u<=0x4000000u&&words(camera+0x194u,&aspect,1)&&aspect==0x3fe38e39u){
   selected_context=context;selected_camera=camera;
  }
 }
 static unsigned reported;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(applied&&trace&&!strcmp(trace,"1")&&reported<8){
  uint32_t selected[2]={0},camera=0,aspect=0;
  int selected_valid=words(context+4u,selected,2);
  int aspect_valid=words(TOPSPIN_SELECTOR_CAMERA,&camera,1)
      &&camera>=0x10000u&&(uint64_t)camera+0x198u<=0x4000000u&&words(camera+0x194u,&aspect,1);
  ++reported;
  fprintf(stderr,"[%s-VIDEO-SELECTOR] caller=%08x requested_mode=8 context=%08x selected_valid=%d selected_size=%ux%u camera=%08x aspect_valid=%d aspect_raw=%08x selector_al=%u\n",
          TOPSPIN_SELECTOR_LABEL,args[0],context,selected_valid,selected[0],selected[1],camera,aspect_valid,aspect,g_eax&255u);
 }
}

/* Read-only selected-title state, rechecked before each canvas request. */
int topspin_selector_video_selected_size(const char*label,uint32_t out[2])
{
 const char*wide=getenv("RECOMP_VIDEO_WIDESCREEN"),*mode=getenv("RECOMP_VIDEO_MODE"),*off=getenv("TOPSPIN_NATIVE720");
 uint32_t camera=0,aspect=0,size[2],record[4];
 if(!label||strcmp(label,TOPSPIN_SELECTOR_LABEL)||!out||!selected_context||!selected_camera
    ||!wide||strcmp(wide,"1")||!mode||strcmp(mode,"720p")||(off&&!strcmp(off,"0")))return 0;
 if(!words(TOPSPIN_SELECTOR_RECORD8,record,4)||record[0]!=1280||record[1]!=720||record[2]!=1||record[3]!=1
    ||!words(selected_context+4u,size,2)||size[0]!=1280||size[1]!=720
    ||!words(TOPSPIN_SELECTOR_CAMERA,&camera,1)||camera!=selected_camera
    ||!words(camera+0x194u,&aspect,1)||aspect!=0x3fe38e39u)return 0;
 memcpy(out,size,sizeof size);return 1;
}
