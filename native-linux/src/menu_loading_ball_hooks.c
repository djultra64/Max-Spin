/* Menu's Loading.pml Progress bitmap animation, matching the uniform-fit
 * LoadEng00.jpg composition. No asset or general HUD changes. djultra64. */
#include "loading_layout.h"
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fenv.h>
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_0001D9F0_gen(void);
extern void __real_sub_000CB820(void);
static _Thread_local int loading_ball;
static int range(uint32_t p,uint32_t n)
{return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static uint32_t word(uint32_t p)
{uint32_t v;memcpy(&v,(void*)((uintptr_t)g_xbox_mem_offset+p),4);return v;}
static int separate(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return (uint64_t)a+an<=b||(uint64_t)b+bn<=a;}
void sub_0001D9F0(void)
{
 uint32_t size[2]={0};int previous=loading_ball;
 /* Static owner installed by111AC, embedded HUD ctor1E1B0, vtable+C=1F130.
  * Authority is limited to this original invocation, not persistent HUD data. */
 loading_ball=!previous&&g_ecx==0x2b8a68u&&range(g_ecx,0x255)
   &&word(g_ecx+8)==0x1e59b4u&&topspin_menu_loading_layout_applied()
   &&topspin_native_video_selected_size("MENU",size)&&size[0]==1280&&size[1]==720;
 sub_0001D9F0_gen();
 loading_ball=previous;
}
void __wrap_sub_000CB820(void)
{
 if(!loading_ball){__real_sub_000CB820();return;}
 uint32_t address=g_esp,original_x=0,original_w=0;int applied=0;
 fenv_t environment;fegetenv(&environment);
 if(loading_ball&&range(address,20)&&word(address)==0x1e796u
    &&range(g_ecx,64)&&separate(g_ecx,64,address,20)
    &&separate(g_ecx,64,0x2b8a68u,0x255)){
  float args[4];memcpy(args,(void*)((uintptr_t)g_xbox_mem_offset+address+4),16);
  /* Loading.pml's authored Progress position. Other HUD draws in the same
   * manager, invalid values and altered layouts retain the original mapping. */
  if(args[0]>.374998f&&args[0]<.375002f&&args[1]>.342498f&&args[1]<.342502f
     &&args[2]>0&&args[2]<=1&&args[3]>0&&args[3]<=1){
   original_x=word(address+4);original_w=word(address+12);
   args[0]*=.75f;args[2]*=.75f;
   memcpy((void*)((uintptr_t)g_xbox_mem_offset+address+4),args,4);
   memcpy((void*)((uintptr_t)g_xbox_mem_offset+address+12),args+2,4);applied=1;
  }
 }
 fesetenv(&environment);
 __real_sub_000CB820();
 /* Preserve the original callee's complete CPU effects. Only borrowed input
  * words are restored after its ret16; its matrix output remains intentional. */
 if(applied){
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+address+4),&original_x,4);
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+address+12),&original_w,4);
  static unsigned logs;const char*trace=getenv("TOPSPIN_VIDEO_TRACE");
  if(trace&&!strcmp(trace,"1")&&logs++<4)
   fprintf(stderr,"[MENU-LOADING-BALL] caller=0001e796 owner=002b8a68 center=1120,606.6->1000,606.6 horizontal_scale=0.75 original_calls=1\n");
 }
}
