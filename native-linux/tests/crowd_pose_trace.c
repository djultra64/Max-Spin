/* CPU-only observational wrapper contract. Own work: djultra64. */
#define RECOMP_GENERATED_CODE
#include "actor_binding_cpu_guard.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_itail_site;
RECOMP_TLS double g_fp_stack[8];RECOMP_TLS int g_fp_top,g_fp_cmp,g_df;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
ptrdiff_t g_xbox_mem_offset;int g_force_return;uint32_t g_xbox_code_lo,g_xbox_code_hi;

extern void sub_000B7C70(void);
static unsigned char *memory;
static VabGuestCpu before;
static unsigned calls;
static int invalid;
static void same(const VabGuestCpu *a,const VabGuestCpu *b){
 assert(!memcmp(a->gpr,b->gpr,sizeof a->gpr));assert(!memcmp(a->fp,b->fp,sizeof a->fp));
 assert(!memcmp(a->xmm,b->xmm,sizeof a->xmm));assert(!memcmp(a->mm,b->mm,sizeof a->mm));
 assert(a->top==b->top&&a->cmp==b->cmp&&a->df==b->df&&a->cw==b->cw&&a->cc==b->cc);
 assert(a->memory_offset==b->memory_offset&&a->force_return==b->force_return&&a->code_lo==b->code_lo&&a->code_hi==b->code_hi);
 assert(!memcmp(&a->environment,&b->environment,sizeof a->environment));
}
void sub_000B7C70_crowd_trace_original(void){
 VabGuestCpu actual;vab_cpu_save(&actual);same(&before,&actual);calls++;
 if(!invalid)memory[g_ecx+0x38]=memory[0x20269];
 g_eax=0xabc000+calls;g_xmm3.u[2]=calls;g_mm7.q=calls;g_itail_site^=9;g_df=1;g_fp_cc=0x4000;
 assert(!fesetround(FE_UPWARD));feraiseexcept(FE_INEXACT);
}
int main(int argc,char**argv){
 assert(argc==2);unsigned count=1;
 invalid=!strcmp(argv[1],"invalid");
 if(!strcmp(argv[1],"dedup"))count=6;
 else if(!strcmp(argv[1],"budget"))count=40;
 else assert(invalid||!strcmp(argv[1],"good"));
 memory=calloc(1,0x4000000);assert(memory);g_xbox_mem_offset=invalid?0:(ptrdiff_t)(uintptr_t)memory;
 uint32_t anim=0x20000;memcpy(memory+0x10020,&anim,4);
 g_ecx=invalid?0xffffffff:0x10000;g_esp=0x90000;g_fp_control_word=0x37f;g_fp_stack[3]=.125;
 for(unsigned i=0;i<count;i++){
  memory[0x20269]=!strcmp(argv[1],"budget")?(i&1u):(i>=4?0:1);
  vab_cpu_save(&before);sub_000B7C70();
  assert(g_eax==0xabc000+calls&&g_xmm3.u[2]==calls&&g_mm7.q==calls);
  assert(fegetround()==FE_UPWARD&&fetestexcept(FE_INEXACT));
 }
 uint32_t hash=2166136261u;for(unsigned i=0;i<0x4000000;i++)hash=(hash^memory[i])*16777619u;
 printf("PASS calls=%u memory=%08X eax=%08X\n",calls,hash,g_eax);free(memory);
}
