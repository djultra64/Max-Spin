/* Exact original RRTexture adapter. Own work: djultra64. */
#include "menu_prompt_runtime.h"
extern void sub_000CB110_gen(void);
extern void sub_000CADB0_gen(void);
extern void sub_000CB7C0_gen(void);
void sub_000CB110(void){topspin_prompt_loaded_hook(sub_000CB110_gen);}
void sub_000CADB0(void){topspin_prompt_retire_hook(sub_000CADB0_gen);}
void sub_000CB7C0(void){topspin_prompt_bind_hook(sub_000CB7C0_gen);}
