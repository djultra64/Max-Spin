/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_0018DF50_gen(void);
extern void sub_0018DC40_gen(void);
extern void sub_00187F90_gen(void);
void sub_0018DF50(void){topspin_prompt_loaded_hook(sub_0018DF50_gen);}
void sub_0018DC40(void){topspin_prompt_retire_hook(sub_0018DC40_gen);}
void sub_00187F90(void){topspin_prompt_bind_hook(sub_00187F90_gen);}
