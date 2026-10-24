#define _POSIX_C_SOURCE 200809L
/* Host bootstrap for authenticated Menu UI sprites. Own work djultra64. */
#include "sprite_ui_admission.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a/nv2a_pgraph_d3d11.h"
#include "d3d8_vk_snapshot.h"
#include "d3d/d3d8_sprite_xbr.h"
#include "kernel/nv2a_executor_job.h"
extern ptrdiff_t g_xbox_mem_offset;
static const void*map_guest(void*u,uint32_t va,size_t n){(void)u;uint64_t end=(uint64_t)va+n;if(!n||!((va>=0x10000&&end<=0x4000000)||(va>=0x80000000u&&end<=0x84000000u)))return NULL;return(const void*)((uintptr_t)g_xbox_mem_offset+va);}
/* Main calls after guest mapping, before guest/GPU workers, Menu only.
 * Cache path is explicit, not a mutable legacy prompt cache. */
int topspin_sprite_ui_initialize(const char*image){const char*p=getenv("TOPSPIN_SPRITE_UI_CACHE");return p&&*p&&image&&!strcmp(image,"menu")&&sxui_configure(p,image,map_guest,NULL);}

int topspin_sprite_ui_register_backend(void){if(!sxui_available())return 0;if(d3d8_vk_sprite_filter_requested()==1)return 1;const char*invoke=getenv("RECOMP_EXECUTOR_INVOKE");if(invoke&&strcmp(invoke,"1"))return 0;if(!invoke&&setenv("RECOMP_EXECUTOR_INVOKE","1",1))return 0;if(!d3d8_vk_snapshot_executor_configure())return 0;pgraph_d3d11_set_ui_texture_admission_callback(sxui_match);return 1;}
