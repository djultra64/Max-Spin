/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_001406B0_gen(void);
extern void sub_00140350_gen(void);
extern void sub_00140D60_gen(void);
void sub_001406B0(void){topspin_prompt_loaded_hook(sub_001406B0_gen);}
void sub_00140350(void){topspin_prompt_retire_hook(sub_00140350_gen);}
void sub_00140D60(void){topspin_prompt_bind_hook(sub_00140D60_gen);}
