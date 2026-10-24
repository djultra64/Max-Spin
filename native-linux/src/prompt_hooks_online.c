/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_00219DE0_gen(void);
extern void sub_00219A80_gen(void);
extern void sub_0021A490_gen(void);
void sub_00219DE0(void){topspin_prompt_loaded_hook(sub_00219DE0_gen);}
void sub_00219A80(void){topspin_prompt_retire_hook(sub_00219A80_gen);}
void sub_0021A490(void){topspin_prompt_bind_hook(sub_0021A490_gen);}
