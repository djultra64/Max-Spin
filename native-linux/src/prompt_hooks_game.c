/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_0012E4B0_gen(void);
extern void sub_0012E1A0_gen(void);
extern void sub_001284C0_gen(void);
void sub_0012E4B0(void){topspin_prompt_loaded_hook(sub_0012E4B0_gen);}
void sub_0012E1A0(void){topspin_prompt_retire_hook(sub_0012E1A0_gen);}
void sub_001284C0(void){topspin_prompt_bind_hook(sub_001284C0_gen);}

/* Exact HUDListDraw emissions; preserve the original +48 bound shadow. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <string.h>
#include <fenv.h>
#include "material_guest_cpu.h"
extern void sub_00078B30_gen(void);
extern void sub_00078AF0_gen(void);
static void hud_consumer(void(*original_fn)(void),int up){
 uint32_t caller=g_esp>=0x10000u&&(uint64_t)g_esp+4<=0x4000000u?MEM32(g_esp):0;
 if(up?(caller!=0x000bdb5du&&caller!=0x000bdd9au):caller!=0x000bdda9u){original_fn();return;}
 Cpu before;cpu_save(&before);uint32_t object=g_ecx,original=topspin_prompt_hud_begin(object,caller);cpu_restore(&before);
 original_fn();
 Cpu after;cpu_save(&after);topspin_prompt_hud_end(object,original);cpu_restore(&after);
}
void sub_00078B30(void){hud_consumer(sub_00078B30_gen,1);}
void sub_00078AF0(void){hud_consumer(sub_00078AF0_gen,0);}
