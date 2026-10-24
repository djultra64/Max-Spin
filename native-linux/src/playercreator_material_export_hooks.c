/* Original Save format/source gate; no replacement encoder. djultra64. */
#include "material_save_session.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <string.h>
#include <fenv.h>
#include "material_guest_cpu.h"
extern void sub_00073320_gen(void);
extern void sub_0007AD70_gen(void);
extern void sub_0007AE10_gen(void);
static uint32_t argument(unsigned n){uint32_t value=0;uint64_t a=(uint64_t)g_esp+4+n*4;if(a>=0x10000u&&a+4<=0x4000000u)memcpy(&value,(void*)XBOX_PTR((uint32_t)a),4);return value;}
void sub_0007AD70(void){Cpu saved;cpu_save(&saved);mc_save_export_validate(argument(1),argument(2));cpu_restore(&saved);sub_0007AD70_gen();}
void sub_0007AE10(void){Cpu saved;cpu_save(&saved);mc_save_export_validate(argument(1),argument(2));cpu_restore(&saved);sub_0007AE10_gen();}

void sub_00073320(void){
 uint32_t caller=0;if(g_esp>=0x10000u&&(uint64_t)g_esp+4<=0x4000000u)memcpy(&caller,(void*)XBOX_PTR(g_esp),4);
 sub_00073320_gen();
 /* B9220 has finished the original GPU text draw. Synchronize before733D0/73600
  * can run its CPU-only resize; preserve the original EAX and whole CPU. */
 if((caller==0x733f2u||caller==0x73622u)&&mc_save_active()){Cpu saved;cpu_save(&saved);mc_save_aux_source(g_eax,caller);cpu_restore(&saved);}
}
