/* Shared exact-original Menu/Game loading bridge. Own work: djultra64.
 * Included once per image adapter; all scope is local to that image/thread. */
#include "loading_layout.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fenv.h>
extern _Thread_local uint32_t g_esp,g_eax;
extern ptrdiff_t g_xbox_mem_offset;
extern void LOADING_REAL_OUTER(void),LOADING_REAL_LOCK(void),LOADING_REAL_DRAW(void);
extern int LOADING_SELECTED_SIZE(const char*,uint32_t[2]);
struct Loading {uint32_t vb,data,width,height,stack;int applied;char path[64];};
static _Thread_local struct Loading *loading;
static _Thread_local unsigned loading_depth;
static int range(uint32_t p,uint32_t n){return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static int separate(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return (uint64_t)a+an<=b||(uint64_t)b+bn<=a;}
static int read_bytes(uint32_t p,void*out,uint32_t n)
{if(!range(p,n))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static int trace_enabled(void)
{const char*trace=getenv("TOPSPIN_VIDEO_TRACE");return trace&&!strcmp(trace,"1");}
static int full_target(void)
{
 uint32_t device=0,viewport[6]={0},first=0,targets[2]={0},desc[5]={0},surface=0,width=0,height=0;
 const char *reason="ready";int eligible=0;
 if(!read_bytes(LOADING_DEVICE,&device,4)||!range(device,0x1a0cu))reason="device";
 else if(!read_bytes(device+0xed0u,viewport,sizeof viewport))reason="viewport-range";
 else if(viewport[0]||viewport[1]||viewport[2]!=1280||viewport[3]!=720
    ||viewport[4]!=0||viewport[5]!=0x3f800000u)reason="viewport";
 else if(!read_bytes(device+0x19f4u,&first,4)||!read_bytes(device+0x1a04u,targets,sizeof targets))reason="targets-range";
 else {
  /* Original155A80/Menu =1A8F60/Game surface selection and descriptor decode.
   * SDK136 scales physical attachments independently of these logical units. */
  surface=first==targets[0]?targets[1]:first;
  if(!read_bytes(surface,desc,sizeof desc))reason="surface-range";
  else {
   if(desc[4]){width=(desc[4]&0xfffu)+1;height=((desc[4]>>12)&0xfffu)+1;}
   else {width=1u<<((desc[3]>>20)&15u);height=1u<<((desc[3]>>24)&15u);}
   if(width!=1280||height!=720)reason="extent";else eligible=1;
  }
 }
 static unsigned logs;
 if(trace_enabled()&&logs++<8)
  fprintf(stderr,"[%s-LOADING-TARGET] eligible=%d reason=%s device=%08x viewport=%u,%u,%u,%u z=%08x/%08x first=%08x targets=%08x/%08x surface=%08x descriptor=%08x/%08x extent=%ux%u\n",
   LOADING_LABEL,eligible,reason,device,viewport[0],viewport[1],viewport[2],viewport[3],viewport[4],viewport[5],first,targets[0],targets[1],surface,desc[3],desc[4],width,height);
 return eligible;
}
/* The authentic Game E19F0 format at23560C selects a court and language;
 *2355EC is its Load fallback. Tip/Help graphics are different choices. */
static int loading_path(const char path[64])
{
 static const char fallback[]="d:\\Media\\Loadings\\LoadEng00.jpg";
#ifndef LOADING_COURT_BACKDROPS
 return !memcmp(path,fallback,sizeof fallback);
#else
 (void)fallback;
 static const char prefix[]="d:\\Media\\Loadings\\";
 static const char*stems[]={
  "AcapulcoXX",
  "AdelaideXX",
  "Alexandrie",
  "Antananari",
  "ArthurAshe",
  "BuenosAire",
  "CapTownXXX",
  "Casablanca",
  "Cincinnati",
  "ColombiaXX",
  "Dubaixxxxx",
  "Hamburgxxx",
  "IndeXXXXXX",
  "IndianWell",
  "Indoor10XX",
  "Indoor11XX",
  "Indoor12XX",
  "Indoor13XX",
  "Indoor14XX",
  "Indoor15XX",
  "Indoor16XX",
  "Indoor1xxx",
  "Indoor2XXX",
  "Indoor3XXX",
  "Indoor4XXX",
  "Indoor5XXX",
  "Indoor6XXX",
  "Indoor7XXX",
  "Indoor8XXX",
  "Indoor9XXX",
  "ItaliaXXXX",
  "Lisbonnexx",
  "Load",
  "MiamiXXXXX",
  "MonacoXXXX",
  "RodLaverXX",
  "RollGarros",
  "Seoulxxxxx",
  "Shanghaixx",
  "Singapourx",
  "Sydneyxxxx",
  "Tashkentxx",
  "TennisClub",
  "ValparaPro",
  "ValparaTrg",
  "Wimbledonx",
 };
 const char*nul=memchr(path,0,64);if(!nul)return 0;
 size_t length=(size_t)(nul-path),prefix_length=sizeof prefix-1;
 if(length<prefix_length+9||memcmp(path,prefix,prefix_length))return 0;
 const char*name=path+prefix_length;size_t stem_length=length-prefix_length-9;
 const char*suffix=name+stem_length;
 if((memcmp(suffix,"Eng00.jpg",9)&&memcmp(suffix,"Fre00.jpg",9)&&memcmp(suffix,"Ger00.jpg",9)))return 0;
 for(unsigned i=0;i<sizeof stems/sizeof stems[0];i++){
  if(strlen(stems[i])!=stem_length)continue;
  size_t j=0;
  for(;j<stem_length;j++){
   unsigned char a=(unsigned char)name[j],b=(unsigned char)stems[i][j];
   if(a>='A'&&a<='Z')a+=(unsigned char)('a'-'A');
   if(b>='A'&&b<='Z')b+=(unsigned char)('a'-'A');
   if(a!=b)break;
  }
  if(j==stem_length)return 1;
 }
 return 0;
#endif
}
static int is_loading(struct Loading *state)
{
 uint32_t size[2]={0},args[2]={0},image[5]={0};char path[64]={0};
 const char*gate=getenv("TOPSPIN_LOADING_LAYOUT"),*reason="ready";int eligible=0;
 if(gate&&strcmp(gate,"1"))reason="disabled";
 else if(!LOADING_SELECTED_SIZE(LOADING_LABEL,size))reason="native-owner";
 else if(size[0]!=1280||size[1]!=720)reason="selected-size";
 else if(!read_bytes(g_esp,args,sizeof args))reason="stack-range";
 else if(args[0]!=LOADING_CALLER)reason="caller";
 else if(!range(g_esp,0x28u+sizeof path)||args[1]!=g_esp+0x14u)reason="image-pointer";
 else if(!read_bytes(args[1],image,sizeof image))reason="image-range";
 else if(!((image[1]==640&&image[2]==480)||(image[1]==1024&&image[2]==512)))reason="backing-size";
 else if(!read_bytes(g_esp+0x28u,path,sizeof path))reason="path-range";
 else if(!loading_path(path))reason="path";
 else if(!full_target())reason="target";
 else {state->width=image[1];state->height=image[2];state->stack=g_esp;memcpy(state->path,path,sizeof path);eligible=1;}
 static unsigned logs;
 if(trace_enabled()&&logs++<8)
  fprintf(stderr,"[%s-LOADING-GATE] eligible=%d reason=%s esp=%08x caller=%08x image=%08x backing=%ux%u selected=%ux%u path=%.63s\n",
   LOADING_LABEL,eligible,reason,g_esp,args[0],args[1],image[1],image[2],size[0],size[1],path);
 return eligible;
}
void LOADING_WRAP_OUTER(void)
{
 struct Loading state={0};struct Loading*previous=loading;
 loading=!loading_depth&&is_loading(&state)?&state:NULL;
 ++loading_depth;LOADING_REAL_OUTER();--loading_depth;loading=previous;
 if(state.applied){
  static unsigned logs;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
  if(trace&&!strcmp(trace,"1")&&logs++<4)
   fprintf(stderr,"[%s-LOADING-LAYOUT] source=%.63s content=640x480 backing=%ux%u logical=1280x720 vertices=4 fan=7 original_draws=1\n",LOADING_LABEL,state.path,state.width,state.height);
 }
}
void LOADING_WRAP_LOCK(void)
{
 uint32_t args[3]={0},header[2]={0};
 int selected=loading&&!loading->data&&read_bytes(g_esp,args,sizeof args)
    &&args[0]==LOADING_LOCK_CALLER&&args[2]==0
    &&read_bytes(args[1],header,sizeof header)&&header[0]==0x01000001u
    &&range(header[1],140)&&separate(args[1],12,loading->stack,0x68u);
 LOADING_REAL_LOCK();
 if(selected&&(g_eax&0xf0000000u)==0x80000000u&&(g_eax&0x0fffffffu)==header[1]){
  loading->vb=args[1];loading->data=g_eax;
 }
 static unsigned logs;
 if(loading&&trace_enabled()&&logs++<8)
  fprintf(stderr,"[%s-LOADING-LOCK] selected=%d caller=%08x vb=%08x flags=%08x header=%08x data=%08x returned=%08x accepted=%d\n",
   LOADING_LABEL,selected,args[0],args[1],args[2],header[0],header[1],g_eax,loading->data!=0);
}
void LOADING_WRAP_DRAW(void)
{
 uint32_t args[4]={0},header[2]={0},size[2]={0};
 if(loading&&loading->data&&!loading->applied&&read_bytes(g_esp,args,sizeof args)
    &&args[0]==LOADING_DRAW_CALLER&&args[1]==7&&args[2]==0&&args[3]==4
    /* Original SetStreamSource atGame1AB170/Menu157B90 adds one
     *0x80000 bound-resource reference after Lock. Keep the exact type,
     *original refcount and expected once-bound state; no broad flag mask. */
    &&read_bytes(loading->vb,header,sizeof header)&&header[0]==0x01080001u
    &&header[1]==(loading->data&0x0fffffffu)
    &&LOADING_SELECTED_SIZE(LOADING_LABEL,size)&&size[0]==1280&&size[1]==720&&full_target()){
  /* Lock returns the contiguous alias, not low virtual RAM. Copy only the
   * original112 vertex bytes within its unchanged140-byte allocation. */
  TopspinLoadingVertex original[4],stretched[4];fenv_t environment;fegetenv(&environment);
  memcpy(original,(void*)((uintptr_t)g_xbox_mem_offset+loading->data),sizeof original);
  if(topspin_loading_build_stretched(original,loading->width,loading->height,stretched)){
   memcpy((void*)((uintptr_t)g_xbox_mem_offset+loading->data),stretched,sizeof stretched);loading->applied=1;
  }
  static unsigned geometry_logs;
  if(trace_enabled()&&geometry_logs++<8){
   uint32_t bits[28];memcpy(bits,original,sizeof bits);
   fprintf(stderr,"[%s-LOADING-GEOMETRY] applied=%d xy0=%08x/%08x xy1=%08x/%08x xy2=%08x/%08x z=%08x rhw=%08x color=%08x uv0=%08x/%08x uv2=%08x/%08x\n",
    LOADING_LABEL,loading->applied,bits[0],bits[1],bits[7],bits[8],bits[14],bits[15],bits[2],bits[3],bits[4],bits[5],bits[6],bits[19],bits[20]);
  }
  fesetenv(&environment);
 }
 static unsigned logs;
 if(loading&&trace_enabled()&&logs++<8)
  fprintf(stderr,"[%s-LOADING-DRAW] applied=%d data=%08x caller=%08x primitive=%u first=%u count=%u header=%08x/%08x selected=%ux%u\n",
   LOADING_LABEL,loading->applied,loading->data,args[0],args[1],args[2],args[3],header[0],header[1],size[0],size[1]);
 LOADING_REAL_DRAW();
}
