/* Game-only, original call once, no original rendering invoked extra times.
 * Preserve all guest state around host bookkeeping. Own work: djultra64. */
#include "game_scene_lifecycle_hooks.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <stdatomic.h>
#include "material_guest_cpu.h"
extern void sub_000EDE40_gen(void);
extern void sub_000EDB80_gen(void);
extern void sub_000F3370_gen(void);
extern void sub_000F32D0_gen(void);
extern void sub_000EB620_gen(void);
static TslSink sink;static void*sink_user;
static int snapshots;
static atomic_int publish_failed;
static _Thread_local int publishing;
int topspin_scene_lifecycle_failed(void){return atomic_load_explicit(&publish_failed,memory_order_acquire);}
static int read_guest(void*u,uint32_t va,void*out,size_t n){(void)u;if(va<0x10000u||(uint64_t)va+n>0x4000000u)return 0;memcpy(out,(void*)XBOX_PTR(va),n);return 1;}
int topspin_scene_lifecycle_configure(TslSink publish,void*user){
 const char*l=getenv("TOPSPIN_VISUAL_SCENE_LIFECYCLE"),*s=getenv("TOPSPIN_VISUAL_SCENE_SNAPSHOT");
 int enabled=l&&!strcmp(l,"1"),want=s&&!strcmp(s,"1");if(want&&(!enabled||!publish))return 0;
 if(!tsl_configure(enabled,enabled?read_guest:NULL,NULL))return 0;sink=publish;sink_user=user;snapshots=want;return 1;
}
static void construct(uint32_t kind,void(*original)(void),unsigned pop){
 if(!tsl_enabled()){original();return;}Cpu saved;cpu_save(&saved);uint32_t va=g_ecx,entry=g_esp;uint64_t ticket=tsl_ctor_begin(kind,va);cpu_restore(&saved);
 original();cpu_save(&saved);if(g_esp==entry+pop)tsl_ctor_end(kind,va,ticket,g_eax);else tsl_dtor(kind,va);cpu_restore(&saved);
}
static void destruct(uint32_t kind,void(*original)(void)){
 if(tsl_enabled()){Cpu saved;cpu_save(&saved);tsl_dtor(kind,g_ecx);cpu_restore(&saved);}original();
}
void sub_000EDE40(void){construct(TSL_APP,sub_000EDE40_gen,12);}
void sub_000F3370(void){construct(TSL_VIEW,sub_000F3370_gen,4);}
void sub_000EDB80(void){destruct(TSL_APP,sub_000EDB80_gen);}
void sub_000F32D0(void){destruct(TSL_VIEW,sub_000F32D0_gen);}
void sub_000EB620(void){
 if(snapshots&&!publishing){Cpu saved;cpu_save(&saved);TslSnapshot owned={0};
  if(!topspin_scene_lifecycle_failed())tsl_snapshot(g_ecx,&owned);
  publishing=1;if(sink(sink_user,&owned)!=1)atomic_store_explicit(&publish_failed,1,memory_order_release);publishing=0;cpu_restore(&saved);
 }
 sub_000EB620_gen();
}
