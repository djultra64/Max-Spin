/* Verified per-image mixer output argument filter. Own integration: djultra64. */
#ifndef TOPSPIN_RUMBLE_OUTPUT_GATE_H
#define TOPSPIN_RUMBLE_OUTPUT_GATE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
extern _Thread_local uint32_t g_esp,g_ecx,g_ebx;
extern ptrdiff_t g_xbox_mem_offset;
static int valid(uint32_t p,uint32_t n)
{return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static uint32_t word(uint32_t p)
{uint32_t v;memcpy(&v,(void*)((uintptr_t)g_xbox_mem_offset+p),4);return v;}
static int overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m)
{return(uint64_t)a+n>b&&(uint64_t)b+m>a;}
static int topspin_rumble_output_disabled(uint32_t stack,uint32_t object,uint32_t port,uint32_t caller,uint32_t vtable,uint32_t app_global)
{
 if(port>=4||!valid(stack,16)||word(stack)!=caller
    ||!valid(object,16)||word(object)!=vtable)return 0;
 uint32_t app=word(app_global);
 if(!valid(app,0xa9cu)||word(app+0xa98u)!=0xa9cu
    ||overlaps(stack+4,8,app,0xa9c)||overlaps(stack+4,8,object,16))return 0;
 for(unsigned i=0;i<4;i++){
  uint32_t player=app+12u+48u*i;
  if(word(player+32)==0&&word(player+36)==port){
   uint8_t pref;memcpy(&pref,(void*)((uintptr_t)g_xbox_mem_offset+player+40),1);
   return pref==0;
  }
 }
 return 0;
}
#endif
