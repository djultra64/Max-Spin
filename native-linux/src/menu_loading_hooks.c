/* Exact original Menu LoadEng00.jpg draw route; no SDK/asset changes. djultra64. */
#include "loading_layout.h"
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
extern _Thread_local uint32_t g_esp,g_eax;
extern ptrdiff_t g_xbox_mem_offset;
extern void __real_sub_000CCE50(void),__real_sub_0015B130(void),__real_sub_0015B180(void),__real_sub_00161650(void);
struct Loading {uint32_t vb,data,width,height;int applied;};
static _Thread_local struct Loading *loading;
static _Atomic int loading_layout_applied;
int topspin_menu_loading_layout_applied(void){return atomic_load(&loading_layout_applied);}
static int range(uint32_t p,uint32_t n){return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static int read_bytes(uint32_t p,void*out,uint32_t n)
{if(!range(p,n))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static int read_vertex_bytes(uint32_t p,void*out,uint32_t n)
{if(p<0x80010000u||(uint64_t)p+n>UINT64_C(0x84000000))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static void write_bytes(uint32_t p,const void*src,uint32_t n)
{memcpy((void*)((uintptr_t)g_xbox_mem_offset+p),src,n);}
static int full_target(void)
{
 uint32_t device=0,viewport[6]={0},first=0,targets[2]={0},surface=0,desc[5]={0};
 uint32_t width=0,height=0;int eligible=0;
 if(read_bytes(0x166058u,&device,4)&&range(device,0x1a0cu)
    &&read_bytes(device+0xed0u,viewport,sizeof viewport)
    &&read_bytes(device+0x19f4u,&first,4)&&read_bytes(device+0x1a04u,targets,sizeof targets)){
  /* Exact original155A97..155AFD/155B25..155B4B source and size decode. */
  surface=first==targets[0]?targets[1]:first;
  if(read_bytes(surface,desc,sizeof desc)){
   if(desc[4]){width=(desc[4]&0xfffu)+1;height=((desc[4]>>12)&0xfffu)+1;}
   else {width=1u<<((desc[3]>>20)&15u);height=1u<<((desc[3]>>24)&15u);}
   eligible=viewport[0]==0&&viewport[1]==0&&viewport[2]>=1280&&viewport[3]>=720
       &&width>=1280&&height>=720&&width<=4096&&height<=4096;
  }
 }
 static unsigned logs;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&logs++<4)
  fprintf(stderr,"[MENU-LOADING-STATE] eligible=%d device=%08x cached_viewport=%u,%u,%u,%u surface=%08x format=%08x size=%08x extent=%ux%u\n",
   eligible,device,viewport[0],viewport[1],viewport[2],viewport[3],surface,desc[3],desc[4],width,height);
 return eligible;
}
static int is_loading(struct Loading *state)
{
 uint32_t size[2]={0},args[2]={0},image[5]={0};char path[32]={0};
 const char*gate=getenv("TOPSPIN_LOADING_LAYOUT");
 const char*reason="ready";int eligible=0;
 static const char expected[]="d:\\Media\\Loadings\\LoadEng00.jpg";
 if(gate&&strcmp(gate,"1"))reason="disabled";
 else if(!topspin_native_video_selected_size("MENU",size))reason="native-owner";
 else if(size[0]!=1280||size[1]!=720)reason="selected-size";
 else if(!read_bytes(g_esp,args,sizeof args))reason="stack-range";
 else if(args[0]!=0xa4c0au)reason="caller";
 else if((uint64_t)g_esp+0x28u+sizeof path>0x4000000u||args[1]!=g_esp+0x14u)reason="image-pointer";
 else if(!read_bytes(args[1],image,sizeof image))reason="image-range";
 else if(!((image[1]==640&&image[2]==480)||(image[1]==1024&&image[2]==512)))reason="image-size";
 else if(!read_bytes(g_esp+0x28u,path,sizeof path))reason="path-range";
 else if(memcmp(path,expected,sizeof expected))reason="path";
 else if(!full_target())reason="target";
 else {eligible=1;state->width=image[1];state->height=image[2];}
 /* Before the target guard: absence of STATE must not hide an earlier
  * ownership/path rejection. Diagnostic reads stay within the original
  * validated ranges and never change eligibility. Own work: djultra64. */
 static unsigned logs;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&logs++<4)
  fprintf(stderr,"[MENU-LOADING-GATE] eligible=%d reason=%s esp=%08x caller=%08x image=%08x source=%ux%u selected=%ux%u path=%.31s\n",
   eligible,reason,g_esp,args[0],args[1],image[1],image[2],size[0],size[1],path);
 return eligible;
}

void __wrap_sub_000CCE50(void)
{
 struct Loading state={0};struct Loading*previous=loading;
 loading=!previous&&is_loading(&state)?&state:NULL;
 __real_sub_000CCE50();
 loading=previous;
 if(!previous)atomic_store(&loading_layout_applied,state.applied);
 if(state.applied){
  static unsigned logs;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
  if(trace&&!strcmp(trace,"1")&&logs++<4)
   fprintf(stderr,"[MENU-LOADING-LAYOUT] source=LoadEng00.jpg original=640x480 center=160,0,960,720 edge_columns=160/160 vertices=12 original_draws=1\n");
 }
}
void __wrap_sub_0015B130(void)
{
 uint32_t args[2],address=g_esp;int selected=loading&&!loading->vb&&read_bytes(address,args,sizeof args)
    &&args[0]==0xcc372u&&args[1]==140u;
 if(selected){uint32_t bytes=336;write_bytes(address+4,&bytes,4);}
 __real_sub_0015B130();
 if(selected){
  uint32_t header[3];write_bytes(address+4,&args[1],4);
  if(read_bytes(g_eax,header,sizeof header)&&header[0]==0x01000001u&&range(header[1],336))loading->vb=g_eax;
 }
}
void __wrap_sub_0015B180(void)
{
 uint32_t args[3];int selected=loading&&loading->vb&&read_bytes(g_esp,args,sizeof args)
    &&args[0]==0xcc4e5u&&args[1]==loading->vb&&args[2]==0;
 __real_sub_0015B180();
 if(selected){
  uint32_t data,physical=g_eax&0x0fffffffu;
  /* Resource.Data is a physical offset. Lock returns its contiguous alias;
   * low guest virtual RAM is a different allocation in this runtime. */
  if((g_eax&0xf0000000u)==0x80000000u&&read_bytes(loading->vb+4,&data,4)&&physical==data&&range(physical,336))loading->data=g_eax;
 }
}
void __wrap_sub_00161650(void)
{
 uint32_t args[4],address=g_esp;int applied=0;
 if(loading&&loading->data&&!loading->applied&&read_bytes(address,args,sizeof args)
    &&args[0]==0xcc62bu&&args[1]==7&&args[2]==0&&args[3]==4){
  TopspinLoadingVertex original[4],slices[12];
  if(read_vertex_bytes(loading->data,original,sizeof original)&&topspin_loading_build_backed_slices(original,loading->width,loading->height,slices)){
   uint32_t primitive=8,count=12;
   write_bytes(loading->data,slices,sizeof slices);write_bytes(address+4,&primitive,4);write_bytes(address+12,&count,4);
   loading->applied=1;applied=1;
  }
 }
 __real_sub_00161650();
 if(applied){write_bytes(address+4,&args[1],4);write_bytes(address+12,&args[3],4);}
}
