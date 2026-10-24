/* Same live preference contract, independently verified Training addresses.
 * Own integration: djultra64. Original draw/input/physics are untouched. */
#include "rumble_output_gate.h"
extern void sub_00063190_gen(void);
void sub_00063190(void)
{
 uint32_t stack=g_esp,saved[2];int mute=topspin_rumble_output_disabled(stack,g_ecx,g_ebx,0x50dfbu,0x235c74u,0x405d8cu);
 if(mute){
  uint32_t zero[2]={0,0};memcpy(saved,(void*)((uintptr_t)g_xbox_mem_offset+stack+4),8);
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+stack+4),zero,8);
 }
 sub_00063190_gen();
 if(mute)memcpy((void*)((uintptr_t)g_xbox_mem_offset+stack+4),saved,8);
}
