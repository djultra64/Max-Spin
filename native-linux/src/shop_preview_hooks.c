/* Native720 Shop preview: exact original5F470 transaction, fullbody-equivalent
 * to Creator89090. No other node/subviewport is changed. Own work djultra64. */
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fenv.h>
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_0005F470_gen(void);
static int words(uint32_t va,uint32_t*out,size_t n)
{
 if(va<0x10000u||(uint64_t)va+4u*n>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),4u*n);return 1;
}
void sub_0005F470(void)
{
 uint32_t node=g_ecx,caller=0,table=0,model=0,size[2]={0},v[6]={0};int applied=0;
 int accepted=topspin_native_video_selected_size("SHOP",size);
 int readable=words(g_esp,&caller,1)&&node>=0x10000u&&(uint64_t)node+0xedu<=0x4000000u
   &&words(node,&table,1)&&words(node+0x34,&model,1)&&words(node+0xd0,v,6);
 if(accepted&&size[0]==1280&&size[1]==720&&readable&&caller==0x0005f694u
    &&table==0x001ce9f8u&&model>=0x10000u&&model<0x4000000u
    &&v[0]==320&&v[1]==0&&v[2]==320&&v[3]==480&&v[4]==0&&v[5]==0x3f800000u){
  const uint32_t scaled[4]={640,0,640,720};
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+node+0xd0),scaled,sizeof scaled);applied=1;
 }
 sub_0005F470_gen();
 if(applied)memcpy((void*)((uintptr_t)g_xbox_mem_offset+node+0xd0),v,4u*sizeof(uint32_t));
 /* Read only pre-call snapshots after original; no freed-node inspection.
  * Diagnostic has a fixed eight-tuple cap and never changes guest state. */
 static unsigned used;static uint32_t seen[8][12];
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&used<8){
  uint32_t key[12]={caller,node,table,model,size[0],size[1],v[0],v[1],v[2],v[3],v[4],v[5]};unsigned i;
  for(i=0;i<used;i++)if(!memcmp(seen[i],key,sizeof key))break;
  if(i==used){
   memcpy(seen[used++],key,sizeof key);fenv_t saved;fegetenv(&saved);
   fprintf(stderr,"[SHOP-PREVIEW] caller=%08X node=%08X vtable=%08X model=%08X accepted=%d selected=%ux%u readable=%d rect=%u,%u,%u,%u depth=%08X/%08X applied=%d\n",caller,node,table,model,accepted,size[0],size[1],readable,v[0],v[1],v[2],v[3],v[4],v[5],applied);
   fesetenv(&saved);
  }
 }
}
