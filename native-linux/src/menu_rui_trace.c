/* Observe original orthographic RUI mapping, without changing title state.
 * Menu-only exact symbol/profile. Own work: djultra64. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern void __real_sub_0004CB00(void);
static int words(uint32_t va,uint32_t*out,size_t n)
{
 if(va<0x10000u||n>0x4000000u/4u||(uint64_t)va+n*4u>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),n*4u);return 1;
}
void __wrap_sub_0004CB00(void)
{
 struct sample {uint32_t node,vtable,caller,flag,manager,canvas[2],flags,scales[2],branch;int valid;};
 static struct sample seen[16];static unsigned count;
 struct sample s={0};int report=0;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&count<16){
  s.node=g_ecx;
  s.valid=words(g_esp,&s.caller,1)&&words(s.node,&s.vtable,1)
    &&s.node<=0x03ffffe0u&&words(s.node+0x1cu,&s.flag,1)
    &&words(0x002b969cu,&s.manager,1)&&s.manager>=0x10000u
    &&(uint64_t)s.manager+0x1440u<=0x4000000u
    &&words(s.manager+0x1420u,s.canvas,2)&&words(s.manager+0x1430u,&s.flags,1)
    &&words(s.manager+0x1434u,s.scales,2)&&words(s.manager+0x143cu,&s.branch,1);
  s.flag&=255u;
  report=1;
  for(unsigned i=0;i<count;i++)if(!memcmp(&seen[i],&s,sizeof s)){report=0;break;}
 }
 __real_sub_0004CB00();
 if(report){seen[count++]=s;
  fprintf(stderr,"[MENU-RUI-ORTHO] node=%08x vtable=%08x caller=%08x valid=%d node_aspect_correct=%u manager=%08x canvas=%ux%u manager_aspect_correct=%u manager_branch=%u manual_scale_raw=%08x/%08x\n",
   s.node,s.vtable,s.caller,s.valid,s.flag,s.manager,s.canvas[0],s.canvas[1],(s.flags>>8)&255u,s.branch&255u,s.scales[0],s.scales[1]);
 }
}

extern void __real_sub_0004C820(void);
void __wrap_sub_0004C820(void)
{
 struct sample {uint32_t node,vtable,caller,camera[4],manager,canvas[2];int valid;};
 static struct sample seen[16];static unsigned count;
 struct sample s={0};int report=0;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&count<16){
  s.node=g_ecx;
  s.valid=words(g_esp,&s.caller,1)&&words(s.node,&s.vtable,1)
    &&s.node<=0x03ffffd4u&&words(s.node+0x1cu,s.camera,4)
    &&words(0x002b969cu,&s.manager,1)&&s.manager>=0x10000u
    &&(uint64_t)s.manager+0x1428u<=0x4000000u&&words(s.manager+0x1420u,s.canvas,2);
  report=1;
  for(unsigned i=0;i<count;i++)if(!memcmp(&seen[i],&s,sizeof s)){report=0;break;}
 }
 __real_sub_0004C820();
 if(report){seen[count++]=s;
  fprintf(stderr,"[MENU-RUI-CAMERA] node=%08x vtable=%08x caller=%08x valid=%d aspect_fov_near_far_raw=%08x/%08x/%08x/%08x manager=%08x canvas=%ux%u\n",
   s.node,s.vtable,s.caller,s.valid,s.camera[0],s.camera[1],s.camera[2],s.camera[3],s.manager,s.canvas[0],s.canvas[1]);
 }
}

extern void __real_sub_00137BA0(void);
void __wrap_sub_00137BA0(void)
{
 /* Real adapter takes (aspect,verticalFov,near,far), then writes its
  * projection matrix+90 through original115109. Observe values as raw bits. */
 struct sample {uint32_t renderer,vtable,args[5];int valid;};
 static struct sample seen[16];static unsigned count;
 struct sample s={0};int report=0;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&count<16){
  s.renderer=g_ecx;
  s.valid=words(g_esp,s.args,5)&&words(s.renderer,&s.vtable,1)
    &&(uint64_t)s.renderer+0xd0u<=0x4000000u;
  report=1;
  for(unsigned i=0;i<count;i++)if(!memcmp(&seen[i],&s,sizeof s)){report=0;break;}
 }
 __real_sub_00137BA0();
 if(report){
  uint32_t matrix[16]={0};int matrix_valid=s.valid&&words(s.renderer+0x90u,matrix,16);
  seen[count++]=s;
  fprintf(stderr,"[MENU-RUI-PROJECTION] renderer=%08x vtable=%08x caller=%08x valid=%d aspect_fov_near_far_raw=%08x/%08x/%08x/%08x matrix_valid=%d matrix_diag_raw=%08x/%08x/%08x/%08x\n",
   s.renderer,s.vtable,s.args[0],s.valid,s.args[1],s.args[2],s.args[3],s.args[4],matrix_valid,matrix[0],matrix[5],matrix[10],matrix[15]);
 }
}

extern void __real_sub_00137B80(void);
void __wrap_sub_00137B80(void)
{
 /* Original virtual+18 copies the argument matrix to renderer+10, marks
  * dirty+191, and returns4. Observe the title's model transform before that
  * copy; never alter matrices or the original register/x87 effects. */
 struct sample {uint32_t renderer,vtable,args[2],model[16],projection[16];int valid;};
 struct key {uint32_t caller,linear[9],projection_diag[4];int valid;};
 static struct key seen[16];static unsigned count;
 struct key key={0};
 struct sample s={0};int report=0;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
 if(trace&&!strcmp(trace,"1")&&count<16){
  s.renderer=g_ecx;
  s.valid=words(g_esp,s.args,2)&&words(s.renderer,&s.vtable,1)
    &&(uint64_t)s.renderer+0xd0u<=0x4000000u
    &&words(s.args[1],s.model,16)&&words(s.renderer+0x90u,s.projection,16);
  key.caller=s.args[0];key.valid=s.valid;
  for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)
   key.linear[row*3+col]=s.model[row*4+col];
  for(unsigned i=0;i<4;i++)key.projection_diag[i]=s.projection[i*5];
  report=1;
  for(unsigned i=0;i<count;i++)if(!memcmp(&seen[i],&key,sizeof key)){report=0;break;}
 }
 __real_sub_00137B80();
 if(report){
  seen[count++]=key;
  fprintf(stderr,"[MENU-RUI-MODEL] renderer=%08x vtable=%08x caller=%08x matrix_ptr=%08x valid=%d model_diag_raw=%08x/%08x/%08x/%08x model_translation_raw=%08x/%08x/%08x projection_diag_raw=%08x/%08x/%08x/%08x\n",
   s.renderer,s.vtable,s.args[0],s.args[1],s.valid,
   s.model[0],s.model[5],s.model[10],s.model[15],s.model[12],s.model[13],s.model[14],
   s.projection[0],s.projection[5],s.projection[10],s.projection[15]);
 }
}
