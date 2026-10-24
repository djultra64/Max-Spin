/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_00072170_gen(void);
extern void sub_00071E10_gen(void);
extern void sub_00072820_gen(void);
void sub_00072170(void){topspin_prompt_loaded_hook(sub_00072170_gen);}
void sub_00071E10(void){topspin_prompt_retire_hook(sub_00071E10_gen);}
void sub_00072820(void){topspin_prompt_bind_hook(sub_00072820_gen);}
