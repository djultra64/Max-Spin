/* Exact original Menu texture-consumer adapter. djultra64. */
#ifndef TOPSPIN_MENU_PROMPT_RUNTIME_H
#define TOPSPIN_MENU_PROMPT_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include "input/input_identity.h"
#define TOPSPIN_MENU_PROMPT_BUDGET (4u*1024u*1024u)
typedef struct {
 void*user;
 void*(*map)(void*,uint32_t,size_t);
 uint32_t(*allocate)(void*,uint32_t,uint32_t);
 /* 1: complete supported labels;0: no activity yet;-1: unsupported input. */
 int(*labels)(void*,uint8_t[6]);
 void(*error)(void*,const char*);
 int(*labels9)(void*,uint8_t[9]); /* optional; old callback remains six-only */
} TopSpinMenuPromptPlatform;
/* NULL platform selects real guest memory/contiguous allocator and physical
 * controller labels. Called once before guest work, or lazily by opt-in env.
 * Failure is explicit. Owned guest allocations stay pinned through exit. */
int topspin_menu_prompts_configure_image(const char*image,const char*directory,const TopSpinMenuPromptPlatform*);
int topspin_prompt_labels9(const InputDeviceIdentity*,const uint8_t sources[9],uint8_t labels[9]);
int topspin_prompt_labels(const InputDeviceIdentity*,const uint8_t sources[6],uint8_t labels[6]);
void topspin_prompt_loaded_hook(void(*original)(void));
void topspin_prompt_retire_hook(void(*original)(void));
void topspin_prompt_bind_hook(void(*original)(void));
int topspin_menu_prompts_configure(const char*directory,const TopSpinMenuPromptPlatform*);
int topspin_menu_prompts_error(void);
size_t topspin_menu_prompts_pinned(void);
size_t topspin_menu_prompts_budget(void);
/* Testable real adapter entry points, also called by exact original wrappers. */
void topspin_menu_prompts_loaded(uint32_t resource);
void topspin_menu_prompts_retire(uint32_t resource);
int topspin_menu_prompts_select(uint32_t original_header,uint32_t*selected);
/* Pipeline must route originals to these wrappers and rename bodies _gen. */
void sub_001379D0(void);
void sub_00137670(void);
void sub_00138080(void);
/* Native HUD bind-only substitution; exact original payload identity required. */
int topspin_menu_prompts_select_hud(uint32_t,uint32_t*);
uint32_t topspin_prompt_hud_begin(uint32_t object,uint32_t caller);
void topspin_prompt_hud_end(uint32_t object,uint32_t original);
#endif
