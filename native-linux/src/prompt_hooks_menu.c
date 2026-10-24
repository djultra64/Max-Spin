/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_001379D0_gen(void);
extern void sub_00137670_gen(void);
extern void sub_00138080_gen(void);
void sub_001379D0(void){topspin_prompt_loaded_hook(sub_001379D0_gen);}
void sub_00137670(void){topspin_prompt_retire_hook(sub_00137670_gen);}
void sub_00138080(void){topspin_prompt_bind_hook(sub_00138080_gen);}
