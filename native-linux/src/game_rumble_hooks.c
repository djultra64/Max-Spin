/* Respect live Game vibration preference even for an already active effect.
 * Only the original FC870 mixer output FCAD8/returnFCADB is eligible:
 * EBX is its controller port; AppData uses the same first-human match as
 * original BBEE0. Preserve output countdown, USB transport and ON behavior.
 * Own integration: djultra64. No effect/physics/input state is modified. */
#include "rumble_output_gate.h"
extern void sub_00088FD0_gen(void);
void sub_00088FD0(void)
{
 uint32_t stack=g_esp,saved[2];int mute=topspin_rumble_output_disabled(stack,g_ecx,g_ebx,0xfcadbu,0x22c5f8u,0x412ce4u);
 if(mute){
  uint32_t zero[2]={0,0};memcpy(saved,(void*)((uintptr_t)g_xbox_mem_offset+stack+4),8);
  memcpy((void*)((uintptr_t)g_xbox_mem_offset+stack+4),zero,8);
 }
 sub_00088FD0_gen();
 if(mute)memcpy((void*)((uintptr_t)g_xbox_mem_offset+stack+4),saved,8);
}
