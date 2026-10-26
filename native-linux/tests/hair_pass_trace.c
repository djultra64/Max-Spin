/* CPU-only wrapper doubles, not execution of original game bodies. djultra64. */
#define RECOMP_GENERATED_CODE
#include "hair_pass_trace.h"
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
extern void sub_00079AE0(void),sub_00085450(void),sub_0006BCA0(void),sub_00086810(void);
static unsigned calls[5],nesting,iterations;static int no_memory,bad,nested,skip_rigid,invalid_model;
static unsigned char*memory;static VabGuestCpu expected[5];
static void put(uint32_t at,uint32_t n){memcpy(memory+at,&n,4);}
static void compare(const VabGuestCpu*a,const VabGuestCpu*b){
 assert(!memcmp(a->gpr,b->gpr,sizeof a->gpr));assert(!memcmp(a->fp,b->fp,sizeof a->fp));
 assert(!memcmp(a->xmm,b->xmm,sizeof a->xmm));assert(!memcmp(a->mm,b->mm,sizeof a->mm));
 assert(a->top==b->top&&a->cmp==b->cmp&&a->df==b->df&&a->cw==b->cw&&a->cc==b->cc);
 assert(a->memory_offset==b->memory_offset&&a->force_return==b->force_return&&a->code_lo==b->code_lo&&a->code_hi==b->code_hi);
 assert(!memcmp(&a->environment,&b->environment,sizeof a->environment));
}
static void original_entry(unsigned role){VabGuestCpu actual;vab_cpu_save(&actual);compare(&expected[role],&actual);calls[role]++;}
static void change(unsigned role){
 g_eax=0xabc000+role;g_edx^=0x12345678;g_itail_site^=0x33;g_xmm7.u[3]^=0xdeadbeef;g_mm4.q^=123;
 g_fp_stack[3]=.125+role;g_fp_top=3;g_fp_cmp=2;g_df=1;g_fp_cc=0x4000;assert(!fesetround(FE_UPWARD));feraiseexcept(FE_INEXACT);
}
static void call(unsigned role,void(*function)(void),uint32_t object,uint32_t pass){
 g_ecx=object;g_esp=no_memory?0xffffffffu:0x90000;if(!no_memory){put(g_esp,0x79c0e);put(g_esp+4,role==2?0x70000:pass);put(g_esp+8,0);put(g_esp+12,pass);}
 if(role==4){put(g_esp,0x79afb);put(g_esp+4,0x10010);put(g_esp+8,0x61000);put(g_esp+12,3);}
 if(role==3)put(g_esp+4,0x50000);
 vab_cpu_save(&expected[role]);function();
 assert(g_eax==(role==4?2:0xabc000+role));assert(fegetround()==FE_UPWARD);assert(fetestexcept(FE_INEXACT));
}
void fixture_original_manager(void){
 original_entry(0);
 if(!no_memory){
  if(nested&&!nesting){VabGuestCpu saved;vab_cpu_save(&saved);nesting++;call(0,sub_00079AE0,0x10000,2);nesting--;vab_cpu_restore(&saved);}
  call(4,sub_00086810,0,0);
  call(1,sub_00085450,0x20000,iterations&1u);
 }
 change(0);
}
void fixture_original_model(void){
 original_entry(1);
 if(!skip_rigid){
  if(nested&&!invalid_model&&!nesting){VabGuestCpu saved;vab_cpu_save(&saved);invalid_model=1;nesting++;call(1,sub_00085450,0x20100,0);nesting--;invalid_model=0;vab_cpu_restore(&saved);}
  void(*dispatch)(void)=sub_0006BCA0;call(2,dispatch,0x30000,0);
 }
 change(1);
}
void fixture_original_rigid(void){
 original_entry(2);
 memory[0x30118]=(unsigned char)bad;
 if(!bad)call(3,sub_00068AE0,0x30004,0);
 change(2);
}
void fixture_original_material(void){original_entry(3);change(3);}
void fixture_original_selector(void){original_entry(4);change(4);g_eax=2;}
static void setup(void){
 memory=calloc(1,0x4000000u);assert(memory);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)memory;
 put(0x100a0,0x60000);put(0x100a4,3);put(0x60008,0x20000);
 put(0x100b0,0x61000);put(0x100b4,3);put(0x2fc59c,0xffffffffu);
 put(0x200c4,0x40000);put(0x200c8,1);put(0x40000,0x30000);
 put(0x30000,0x222b9c);put(0x30008,6);memory[0x30081]=1;memory[0x3008c]=0x18;
 put(0x30c148,0x50000);memory[0x500c4]=1;put(0x5019c,0x77777777);put(0x30dbc8,123);
 put(0x5006c,2);
 for(unsigned i=0;i<24;i++)put(0x30c550+i*4,0x3f800000+i);
 for(unsigned i=0;i<16;i++){put(0x30170+i*4,0x40000000+i);put(0x70000+i*4,0x40400000+i);}
 g_eax=9;g_itail_site=0xf1bd5;g_df=1;g_fp_stack[0]=2.5;g_fp_top=7;g_fp_control_word=0x37f;
 g_xmm0.u[0]=123;g_mm0.q=456;g_force_return=7;g_xbox_code_lo=0x10000;g_xbox_code_hi=0x20000;
}
int main(int argc,char**argv){
 assert(argc==2);setup();const char*mode=argv[1];unsigned count=1;
 if(!strcmp(mode,"invalid")){no_memory=1;g_xbox_mem_offset=0;}
 else if(!strcmp(mode,"dedup"))count=6;
 else if(!strcmp(mode,"budget"))count=30;
 else if(!strcmp(mode,"nested"))nested=1;
 else if(!strcmp(mode,"skip"))skip_rigid=1;
 else if(!strcmp(mode,"duplicate"))put(0x60004,0x20000);
 else if(!strcmp(mode,"gateprogram"))memory[0x30081]=0;
 else if(!strcmp(mode,"gatecontext"))put(0x5006c,3);
 else assert(!strcmp(mode,"good")||!strcmp(mode,"bad"));
 for(iterations=0;iterations<count;iterations++){
  bad=!strcmp(mode,"bad")||(!strcmp(mode,"budget")&&(iterations&1u));
  if(!no_memory){memory[0x30118]=0;if(!strcmp(mode,"budget"))put(0x3011c,iterations);}
  call(0,sub_00079AE0,no_memory?0xffffffffu:0x10000,0);
 }
 uint32_t hash=2166136261u;for(unsigned i=0;i<0x4000000u;i++)hash=(hash^memory[i])*16777619u;
 printf("PASS calls=%u/%u/%u/%u selector_calls=%u memory=%08X eax=%08X fenv=%d/%d\n",calls[0],calls[1],calls[2],calls[3],calls[4],hash,g_eax,fegetround(),fetestexcept(FE_INEXACT));free(memory);
}
