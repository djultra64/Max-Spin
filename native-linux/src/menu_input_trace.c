/* Observe original Menu input and bounded OHCI descriptors; no guest writes.
 * Own diagnostics: djultra64. */
#include "menu_input_trace.h"
#include "usb/ohci.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern _Thread_local uint32_t g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern void __real_sub_0009E4E0(void);
static int word(TopSpinMenuInputRead rd,void*u,uint32_t at,uint32_t*out){return rd(u,at,out,4);}
static int physical(TopSpinMenuInputRead rd,void*u,uint32_t at,void*out,size_t n){
 at&=0x0fffffffu;if(at<0x1000||n>0x4000000u||(uint64_t)at+n>0x4000000u)return 0;
 return rd(u,at|0x80000000u,out,n);
}
int topspin_menu_input_snapshot(TopSpinMenuInputSample*s,uint64_t ns,uint32_t phase,uint32_t base,TopSpinMenuInputRead rd,void*u){
 if(!s||!rd)return 0;
 memset(s,0,sizeof*s);s->ns=ns;s->phase=phase;s->base=base?base:0x32e730;
 if(s->base>=0x10000&&(uint64_t)s->base+4*0xdc<=0x4000000){
  int valid=1;
  for(unsigned p=0;p<4;p++){
   valid&=rd(u,s->base+p*0xdc+0xd0,s->port[p],12);
   valid&=rd(u,0x32e6d8+p*0x16,&s->port[p][3],18);
  }
  if(valid)s->valid|=1;
 }
 if(word(rd,u,0x32ebd0,&s->manager)&&s->manager>=0x10000&&(uint64_t)s->manager+0xfc<=0x4000000&&
    word(rd,u,s->manager+0xd8,&s->manager_flags[0])&&word(rd,u,s->manager+0xf8,&s->manager_flags[1]))s->valid|=2;
 static const uint32_t offsets[7]={4,12,16,24,28,32,48};
 int valid=1;for(unsigned i=0;i<7;i++)valid&=word(rd,u,0xfed00000u+offsets[i],&s->ohci[i]);
 if(!valid)return 1;
 s->valid|=4;
 uint32_t heads[32];if(!physical(rd,u,s->ohci[3],heads,sizeof heads))return 1;s->valid|=8;
 for(unsigned root=0;root<32&&s->ed_count<TOPSPIN_MENU_INPUT_EDS;root++){
  uint32_t at=heads[root]&~15u;
  while(at&&s->ed_count<TOPSPIN_MENU_INPUT_EDS){
   unsigned n=0;while(n<s->ed_count&&s->ed[n].va!=at)n++;if(n<s->ed_count)break;
   TopSpinMenuInputEd*e=&s->ed[s->ed_count];e->va=at;
   if(!physical(rd,u,at,e->word,sizeof e->word)){memset(e,0,sizeof*e);break;}
   s->ed_count++;at=e->word[3]&~15u;
  }
 }
 return 1;
}
static struct {char path[768];TopSpinMenuInputSample row[TOPSPIN_MENU_INPUT_SAMPLES];unsigned emitted;FILE*f;} trace;
static atomic_int init;
static atomic_uint published;
static atomic_uint_fast64_t next_due;
static atomic_flag capturing=ATOMIC_FLAG_INIT;
static int enabled(void){
 int status=atomic_load_explicit(&init,memory_order_acquire);if(status)return status==2;
 int expected=0;if(!atomic_compare_exchange_strong(&init,&expected,1))return 0;
 const char*p=getenv("TOPSPIN_MENU_INPUT_TRACE");
 if(!p||!*p||strlen(p)>=sizeof trace.path){atomic_store_explicit(&init,-1,memory_order_release);return 0;}
 memcpy(trace.path,p,strlen(p)+1);atomic_store_explicit(&init,2,memory_order_release);return 1;
}
static int guest_read(void*u,uint32_t at,void*out,size_t n){
 (void)u;if(!out||!n)return 0;
 if(at>=0xfed00000u&&at<=0xfed00030u&&n==4){
  if(!xbox_OhciOwnsAddress(at))return 0;
  /* Original trapped 32-bit reads use the existing OHCI model. These seven
   * registers have no read-to-clear semantics; never write an ACK here. */
  uint32_t value=*(volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+at);memcpy(out,&value,4);return 1;
 }
 uint64_t end=(uint64_t)at+n;
 if(!((at>=0x10000&&end<=0x4000000)||(at>=0x80000000u&&end<=0x84000000u)))return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+at),n);return 1;
}
static void capture(uint64_t ns,uint32_t phase,uint32_t base){
 if(atomic_flag_test_and_set_explicit(&capturing,memory_order_acquire))return;
 unsigned n=atomic_load_explicit(&published,memory_order_relaxed);
 if(n<TOPSPIN_MENU_INPUT_SAMPLES){
  topspin_menu_input_snapshot(&trace.row[n],ns,phase,base,guest_read,NULL);
  atomic_store_explicit(&published,n+1,memory_order_release);
 }
 atomic_flag_clear_explicit(&capturing,memory_order_release);
}
void __wrap_sub_0009E4E0(void){
 if(!enabled()){__real_sub_0009E4E0();return;}
 uint64_t now=SDL_GetTicksNS();uint32_t base=0;
 uint_fast64_t due=atomic_load_explicit(&next_due,memory_order_relaxed);
 int sample=now>=due&&atomic_load_explicit(&published,memory_order_relaxed)+2<=TOPSPIN_MENU_INPUT_SAMPLES
   &&atomic_compare_exchange_strong(&next_due,&due,now+1000000000u);
 if(sample){guest_read(NULL,g_esp+4,&base,4);capture(now,0,base);}
 __real_sub_0009E4E0();
 if(sample)capture(SDL_GetTicksNS(),1,base);
}
void topspin_menu_input_trace_poll(void){
 if(atomic_load_explicit(&init,memory_order_acquire)!=2)return;
 unsigned n=atomic_load_explicit(&published,memory_order_acquire);if(n==trace.emitted)return;
 if(!trace.f){trace.f=fopen(trace.path,"w");if(!trace.f){trace.emitted=n;return;}fputs("ns,phase,base,valid,manager,removed_mask,check_removed,port,handle,inserted,removed,raw0,raw1,raw2,raw3,raw4,ohci_control,ohci_status,ohci_enable,hcca,current_ed,control_head,done_head,ed_index,ed_va,ed_control,ed_tail,ed_head,ed_next\n",trace.f);}
 for(;trace.emitted<n;trace.emitted++){
  const TopSpinMenuInputSample*s=&trace.row[trace.emitted];
  unsigned rows=s->ed_count>4?s->ed_count:4;
  for(unsigned row=0;row<rows;row++){
   unsigned p=row%4;
   fprintf(trace.f,"%llu,%u,%08x,%x,%08x,%08x,%08x,%u",(unsigned long long)s->ns,s->phase,s->base,s->valid,s->manager,s->manager_flags[0],s->manager_flags[1],p);
   for(unsigned j=0;j<8;j++)fprintf(trace.f,",%08x",s->port[p][j]);
   for(unsigned j=0;j<7;j++)fprintf(trace.f,",%08x",s->ohci[j]);
   unsigned e=row<s->ed_count?row:TOPSPIN_MENU_INPUT_EDS;
   fprintf(trace.f,",%u",e);
   if(e<TOPSPIN_MENU_INPUT_EDS){fprintf(trace.f,",%08x",s->ed[e].va);for(unsigned j=0;j<4;j++)fprintf(trace.f,",%08x",s->ed[e].word[j]);}
   else fputs(",0,0,0,0,0",trace.f);
   fputc('\n',trace.f);
  }
 }
 fflush(trace.f);
}
