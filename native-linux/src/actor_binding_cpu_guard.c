#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "actor_binding_cpu_guard.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
extern RECOMP_TLS uint32_t g_itail_site;
#define REGS(X) X(g_eax,0) X(g_ecx,1) X(g_edx,2) X(g_esp,3) X(g_ebx,4) X(g_esi,5) X(g_edi,6) X(g_ebp,7) X(g_seh_ebp,8) X(g_fs_base,9) X(g_itail_site,10)
#define XMMS(X) X(g_xmm0,0) X(g_xmm1,1) X(g_xmm2,2) X(g_xmm3,3) X(g_xmm4,4) X(g_xmm5,5) X(g_xmm6,6) X(g_xmm7,7)
#define MMS(X) X(g_mm0,0) X(g_mm1,1) X(g_mm2,2) X(g_mm3,3) X(g_mm4,4) X(g_mm5,5) X(g_mm6,6) X(g_mm7,7)
void vab_cpu_save(VabGuestCpu*s){memset(s,0,sizeof *s);
#define SAVE(r,i) s->gpr[i]=r;
REGS(SAVE)
#undef SAVE
#define SAVE(r,i) s->xmm[i]=r;
XMMS(SAVE)
#undef SAVE
#define SAVE(r,i) s->mm[i]=r;
MMS(SAVE)
#undef SAVE
memcpy(s->fp,g_fp_stack,sizeof s->fp);s->top=g_fp_top;s->cmp=g_fp_cmp;s->df=g_df;s->cw=g_fp_control_word;s->cc=g_fp_cc;s->memory_offset=g_xbox_mem_offset;s->force_return=g_force_return;s->code_lo=g_xbox_code_lo;s->code_hi=g_xbox_code_hi;if(fegetenv(&s->environment))abort();}
void vab_cpu_restore(const VabGuestCpu*s){
#define RESTORE(r,i) r=s->gpr[i];
REGS(RESTORE)
#undef RESTORE
#define RESTORE(r,i) r=s->xmm[i];
XMMS(RESTORE)
#undef RESTORE
#define RESTORE(r,i) r=s->mm[i];
MMS(RESTORE)
#undef RESTORE
memcpy(g_fp_stack,s->fp,sizeof s->fp);g_fp_top=s->top;g_fp_cmp=s->cmp;g_df=s->df;g_fp_control_word=s->cw;g_fp_cc=s->cc;g_xbox_mem_offset=s->memory_offset;g_force_return=s->force_return;g_xbox_code_lo=s->code_lo;g_xbox_code_hi=s->code_hi;if(fesetenv(&s->environment))abort();}
void vab_cpu_observe_call(void(*original)(void),void(*probe)(void*,unsigned),void*user){assert(original);if(!probe){original();return;}VabGuestCpu cpu;vab_cpu_save(&cpu);probe(user,0);vab_cpu_restore(&cpu);original();vab_cpu_save(&cpu);probe(user,1);vab_cpu_restore(&cpu);}
