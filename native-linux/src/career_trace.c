/* No guest writes, filesystem inspection, synthetic input or GPU operations.
 * Own diagnostics: djultra64. */
#include "career_trace.h"
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern _Thread_local uint32_t g_eax,g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
enum {CAPACITY=128};
typedef struct {
 uint64_t ns;
 uint32_t event,image,stage,phase,eax,ecx,esp,caller,args[3],globals[3],valid;
 uint32_t save_owner,model_owner,item_array,first_item,path_ptr,descriptor,container,mode,packflag,prereq_valid;
 char target[260];
} Row;
static struct {char path[768];FILE*file;Row rows[CAPACITY];unsigned emitted[CAPACITY];int failed;} trace;
static atomic_int enabled;
static atomic_uint reserved;
static atomic_int ready[CAPACITY];
int topspin_career_trace_init(void)
{
 const char*p=getenv("TOPSPIN_CAREER_TRACE");
 if(!p||!*p){atomic_store_explicit(&enabled,0,memory_order_release);return 1;}
 if(strlen(p)>=sizeof trace.path)return 0;
 memcpy(trace.path,p,strlen(p)+1);
 atomic_store_explicit(&enabled,1,memory_order_release);return 1;
}
static int read_words(uint32_t at,void*out,unsigned n)
{
 if(at<0x10000u||(uint64_t)at+n*4u>0x4000000u)return 0;
 memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+at),n*4u);return 1;
}
/* Original Creator629D0 prerequisites andC6A30/C66A0 descriptor layout.
 * Entry-only: originals may free/reuse these objects before returning. */
static void prerequisites(Row*r)
{
 if(r->image!=TOPSPIN_CAREER_CREATOR||r->stage<TOPSPIN_CAREER_SAVE_CALLBACK)return;
 if(r->stage==TOPSPIN_CAREER_SAVE_CALLBACK){
  if(read_words(0x390a5cu,&r->save_owner,1)&&read_words(0x390a54u,&r->model_owner,1))r->prereq_valid|=1;
  if(r->model_owner>=0x10000u&&(uint64_t)r->model_owner+16<=0x4000000u
      &&read_words(r->model_owner+12,&r->item_array,1))r->prereq_valid|=2;
  if(read_words(r->item_array,&r->first_item,1))r->prereq_valid|=4;
  if(r->first_item>=0x10000u&&(uint64_t)r->first_item+0x94<=0x4000000u
      &&read_words(r->first_item+0x90,&r->path_ptr,1))r->prereq_valid|=8;
 }else if(r->valid&1){
  r->descriptor=r->args[0];
  if(r->descriptor>=0x10000u&&(uint64_t)r->descriptor+0x121<=0x4000000u){
   read_words(r->descriptor+0x104,&r->mode,1);
   read_words(r->descriptor+0x114,&r->container,1);
   memcpy(&r->packflag,(void*)((uintptr_t)g_xbox_mem_offset+r->descriptor+0x11c),1);
   r->path_ptr=r->descriptor;r->prereq_valid|=16;
  }
 }
 if(r->path_ptr>=0x10000u&&r->path_ptr<0x4000000u){
  unsigned n=sizeof r->target;
  if((uint64_t)r->path_ptr+n>0x4000000u)n=0x4000000u-r->path_ptr;
  const char*p=(const char*)((uintptr_t)g_xbox_mem_offset+r->path_ptr);
  const char*end=memchr(p,0,n);
  if(end){memcpy(r->target,p,(size_t)(end-p));r->prereq_valid|=32;}
 }
}
static void csv_target(FILE*f,const char*p)
{
 fputc('"',f);for(;*p;p++){if(*p=='"')fputc('"',f);fputc(*p,f);}fputc('"',f);
}
static void current(Row*r)
{
 r->ns=SDL_GetTicksNS();r->eax=g_eax;r->ecx=g_ecx;r->esp=g_esp;
 if(r->image==TOPSPIN_CAREER_MENU&&read_words(0x3bcabcu,r->globals,3))r->valid|=2;
}
unsigned topspin_career_trace_begin(unsigned image,unsigned stage)
{
 if(!atomic_load_explicit(&enabled,memory_order_acquire))return 0;
 unsigned at=atomic_load_explicit(&reserved,memory_order_relaxed);
 do{if(at>=CAPACITY)return 0;}
 while(!atomic_compare_exchange_weak_explicit(&reserved,&at,at+2,memory_order_relaxed,memory_order_relaxed));
 Row*r=&trace.rows[at];r->event=at/2;r->image=image;r->stage=stage;
 uint32_t words[4];if(read_words(g_esp,words,4)){r->caller=words[0];memcpy(r->args,words+1,12);r->valid|=1;}
 prerequisites(r);current(r);atomic_store_explicit(&ready[at],1,memory_order_release);return at+1;
}
void topspin_career_trace_end(unsigned token)
{
 if(!token||token>CAPACITY||(token&1u)==0)return;
 unsigned at=token-1;Row*r=&trace.rows[at+1];*r=trace.rows[at];
 r->phase=1;current(r);atomic_store_explicit(&ready[at+1],1,memory_order_release);
}
void topspin_career_trace_poll(void)
{
 if(!atomic_load_explicit(&enabled,memory_order_acquire)||trace.failed)return;
 unsigned count=atomic_load_explicit(&reserved,memory_order_relaxed);
 for(unsigned i=0;i<count;i++){
  if(trace.emitted[i]||!atomic_load_explicit(&ready[i],memory_order_acquire))continue;
  if(!trace.file){
   trace.file=fopen(trace.path,"w");
   if(!trace.file){trace.failed=1;fputs("[CAREER-TRACE] Cannot open the requested diagnostic CSV\n",stderr);return;}
   fputs("ns,event,image,stage,phase,eax,al,ecx,esp,caller,arg0,arg1,arg2,valid,allocator,directory,package,save_owner,model_owner,item_array,first_item,path_ptr,descriptor,container,mode,packflag,prereq_valid,target\n",trace.file);
  }
  const Row*r=&trace.rows[i];
  const char*stage=r->stage==TOPSPIN_CAREER_VALIDATE?"validate":r->stage==TOPSPIN_CAREER_PACK_LOAD?"pack_load":r->stage==TOPSPIN_CAREER_SAVE_WRITE?"save_write":r->stage==TOPSPIN_CAREER_PACK_SCAN?"pack_scan":r->stage==TOPSPIN_CAREER_SAVE_CALLBACK?"save_callback":r->stage==TOPSPIN_CAREER_SAVE_DISPATCH?"save_dispatch":r->stage==TOPSPIN_CAREER_SAVE_DESCRIPTOR?"save_descriptor":"pack_serialize";
  fprintf(trace.file,"%llu,%u,%u,%s,%u,%08x,%02x,%08x,%08x,%08x,%08x,%08x,%08x,%x,%08x,%08x,%08x,",
    (unsigned long long)r->ns,r->event,r->image,stage,r->phase,r->eax,r->eax&255u,r->ecx,r->esp,r->caller,r->args[0],r->args[1],r->args[2],r->valid,r->globals[0],r->globals[1],r->globals[2]);
  fprintf(trace.file,"%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%02x,%x,",r->save_owner,r->model_owner,r->item_array,r->first_item,r->path_ptr,r->descriptor,r->container,r->mode,r->packflag,r->prereq_valid);
  csv_target(trace.file,r->target);fputc('\n',trace.file);
  trace.emitted[i]=1;
 }
 if(trace.file)fflush(trace.file);
}
