/* CPU pass-gate forensics only. No guest writes or rendering changes. djultra64. */
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "hair_pass_trace.h"
#include "actor_binding_cpu_guard.h"
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HPT_TIERS 8u
#define HPT_CHILDREN 256u
#define HPT_HEADS 16u
#define HPT_MODELS 4u
#define HPT_DEPTH 8u
#define HPT_LINE 12288u
#define HPT_VTABLE 0x222b9cu
typedef struct {
 uint32_t leaf,type,flags,visible,hidden,mode,bypass,context,c4,ctx_mode;
 uint32_t bounds[12],matrix[16],frustum[24];
} HptState;
typedef struct {
 HptState model_before,model_after,rigid_before,rigid_after;
 uint32_t attempts,materials,matrix_arg,matrix[16],material_c4[2],material_ctx[2];
 uint32_t material_this[2],material_enabled7d[2],material_arg_ctx[2],material_ctx6c[2];
} HptHead;
typedef struct HptManager {
 struct HptManager *previous;
 uint32_t manager,pass,counter,caller,tier_count,tier_heads,invalid,models,heads,rigids,materials,model_limit;
 uint32_t manager_mode,manager_dirty,manager_counter,context_before,c4_before,ctx_mode_before;
 uint32_t selector_count,selector_first,selector_last,selector_values[4],selector_callers[4],lod_global,thresholds,threshold_count;
 unsigned sequence,depth,admitted;char summary[HPT_LINE/2];size_t summary_used;
} HptManager;
typedef struct HptModel {
 struct HptModel *previous;HptManager *manager;
 uint32_t model,pass,lod,tier_mask,caller,head_count,truncated,context_before,c4_before,mode_before,admitted;
 HptHead head[HPT_HEADS];
} HptModel;
typedef struct HptRigid {struct HptRigid *previous;HptModel *model;HptHead *head;} HptRigid;
typedef struct {uint32_t manager,model,leaf,pass,signature[21];unsigned uses;} HptSeen;
static _Atomic int configured; /* 0 unknown,1 initializing,2 off,3 on */
static uint32_t minimum_counter;
static _Atomic unsigned managers,snapshots;
static RECOMP_TLS HptManager *manager_scope;
static RECOMP_TLS HptModel *model_scope;
static RECOMP_TLS HptRigid *rigid_scope;
static RECOMP_TLS HptSeen seen[128];
static RECOMP_TLS unsigned next_seen;

static int valid(uint32_t at,size_t bytes){
 uint64_t end=(uint64_t)at+bytes;
 return bytes && ((at>=0x10000u&&end<=0x04000000ull)||(at>=0x80000000u&&end<=0x84000000ull));
}
static int read_bytes(uint32_t at,void*out,size_t bytes){
 if(!valid(at,bytes)){memset(out,0,bytes);return 0;}
 memcpy(out,(const void*)((uintptr_t)g_xbox_mem_offset+at),bytes);return 1;
}
static uint32_t word(uint32_t at){uint32_t value;read_bytes(at,&value,4);return value;}
static uint32_t byte(uint32_t at){unsigned char value;read_bytes(at,&value,1);return value;}
static uint32_t argument(const VabGuestCpu*cpu,unsigned index){
 uint64_t at=(uint64_t)cpu->gpr[3]+4u*index;
 return at<=UINT32_MAX?word((uint32_t)at):0;
}
static int enabled(void){
 int value=atomic_load_explicit(&configured,memory_order_acquire);
 if(!value){int expected=0;if(atomic_compare_exchange_strong_explicit(&configured,&expected,1,memory_order_acq_rel,memory_order_acquire)){
   const char*e=getenv("TOPSPIN_HAIR_PASS_TRACE");int on=e&&!strcmp(e,"1");
   if(on){e=getenv("TOPSPIN_HAIR_PASS_TRACE_MIN_COUNTER");if(e&&*e){char*end;errno=0;unsigned long n=strtoul(e,&end,0);if(!errno&&!*end&&n<=UINT32_MAX)minimum_counter=(uint32_t)n;}}
   value=on?3:2;atomic_store_explicit(&configured,value,memory_order_release);
  }else value=expected;}
 return value==3;
}
static int reserve(_Atomic unsigned*counter,unsigned limit,unsigned*out){
 unsigned n=atomic_load_explicit(counter,memory_order_relaxed);
 while(n<limit){if(atomic_compare_exchange_weak_explicit(counter,&n,n+1,memory_order_relaxed,memory_order_relaxed)){*out=n;return 1;}}
 return 0;
}
static void append(char*line,size_t cap,size_t*used,const char*format,...){
 if(*used>=cap-1)return;
 va_list args;va_start(args,format);int n=vsnprintf(line+*used,cap-*used,format,args);va_end(args);
 if(n<0)return;
 *used+=(size_t)n>=cap-*used?cap-*used-1:(size_t)n;
}
static void array(char*line,size_t cap,size_t*used,const char*name,const uint32_t*data,unsigned count){
 append(line,cap,used," %s=",name);for(unsigned i=0;i<count;i++)append(line,cap,used,"%s%08X",i?",":"",data[i]);
}
static int head_leaf(uint32_t leaf){return valid(leaf,0x1c1)&&word(leaf)==HPT_VTABLE&&(byte(leaf+0x8c)&0x10u);}
static uint32_t context_state(uint32_t* c4,uint32_t* mode){
 uint32_t context=word(0x30c148);*c4=valid(context,0x1a0)?byte(context+0xc4):UINT32_MAX;
 *mode=valid(context,0x1a0)?word(context+0x19c):UINT32_MAX;return context;
}
static void snapshot(HptState*s,uint32_t leaf){
 memset(s,0,sizeof*s);s->leaf=leaf;s->type=word(leaf+8);s->visible=byte(leaf+0x81);
 s->flags=byte(leaf+0x8c);s->hidden=byte(leaf+0x118);s->mode=word(leaf+0x11c);s->bypass=byte(leaf+0x1c0);
 s->context=context_state(&s->c4,&s->ctx_mode);
 read_bytes(leaf+0xec,s->bounds,sizeof s->bounds);read_bytes(leaf+0x170,s->matrix,sizeof s->matrix);
 read_bytes(0x30c550,s->frustum,sizeof s->frustum);
}
static unsigned model_heads(uint32_t model,HptModel*out){
 if(!valid(model,0x154))return 0;
 uint32_t table=word(model+0xc4),count=word(model+0xc8);unsigned found=0;
 if(count>HPT_CHILDREN){if(out)out->truncated=1;count=HPT_CHILDREN;}
 if(!count||!valid(table,(size_t)count*12))return 0;
 for(uint32_t i=0;i<count;i++){
  uint32_t leaf=word(table+i*12);if(!head_leaf(leaf))continue;found++;
  if(out){if(out->head_count<HPT_HEADS){HptHead*h=&out->head[out->head_count++];snapshot(&h->model_before,leaf);}else out->truncated=1;}
 }
 return found;
}
static uint32_t tier_mask(uint32_t manager,uint32_t model){
 if(!valid(manager,0xa8))return 0;
 uint32_t table=word(manager+0xa0),count=word(manager+0xa4);
 if(count>HPT_TIERS||!valid(table,(size_t)count*4))return 0;
 uint32_t mask=0;for(uint32_t i=0;i<count;i++)if(word(table+i*4)==model)mask|=1u<<i;
 return mask;
}
static void signature(const HptHead*h,uint32_t out[21]){
 const HptState*a=&h->model_before,*b=&h->model_after;
 uint32_t values[]={a->visible,a->flags,a->hidden,a->mode,a->bypass,a->type,b->visible,b->hidden,b->mode,h->attempts,h->materials,a->c4,a->ctx_mode,h->rigid_before.c4,h->rigid_after.hidden,h->material_c4[0],h->material_c4[1],h->material_enabled7d[0],h->material_enabled7d[1],h->material_ctx6c[0],h->material_ctx6c[1]};
 memcpy(out,values,sizeof values);
}
static int new_signature(HptModel*m,HptHead*h){
 uint32_t state[21],leaf=h->model_before.leaf;signature(h,state);
 for(unsigned i=0;i<128;i++)if(seen[i].uses&&seen[i].manager==m->manager->manager&&seen[i].model==m->model&&seen[i].leaf==leaf&&seen[i].pass==m->pass&&!memcmp(seen[i].signature,state,sizeof state)){
  if(seen[i].uses>=2)return 0;
  seen[i].uses++;return 1;
 }
 HptSeen*s=&seen[next_seen++%128];memset(s,0,sizeof*s);s->manager=m->manager->manager;s->model=m->model;s->leaf=leaf;s->pass=m->pass;memcpy(s->signature,state,sizeof state);s->uses=1;return 1;
}
static void state_text(char*line,size_t*used,const char*name,const HptState*s){
 append(line,HPT_LINE,used," %s=%08X/%u/%02X/%u/%u/%u/%u/%08X/%u/%u",name,s->leaf,s->type,s->flags,s->visible,s->hidden,s->mode,s->bypass,s->context,s->c4,s->ctx_mode);
 char key[40];snprintf(key,sizeof key,"%s_boundsEC_118",name);array(line,HPT_LINE,used,key,s->bounds,12);
 snprintf(key,sizeof key,"%s_matrix170",name);array(line,HPT_LINE,used,key,s->matrix,16);
 snprintf(key,sizeof key,"%s_frustum30C550",name);array(line,HPT_LINE,used,key,s->frustum,24);
}
static void detail(HptModel*m,HptHead*h){
 if(!new_signature(m,h))return;
 unsigned index;if(!reserve(&snapshots,TOPSPIN_HAIR_PASS_SNAPSHOT_LIMIT,&index))return;
 char line[HPT_LINE];size_t used=0;
 append(line,sizeof line,&used,"[HAIR-PASS] detail=%u author=djultra64 counter=%u manager=%08X requested_pass=%u model=%08X pass=%u unique_lod=%u tier_mask=%02X selected_first=%u selected_last=%u leaf=%08X rigid=%u material=%u fields=leaf/type/flags8C/visible81/hidden118/mode11C/bypass1C0/context/c4/ctx19C",index,m->manager->counter,m->manager->manager,m->manager->pass,m->model,m->pass,m->lod,m->tier_mask,m->manager->selector_first,m->manager->selector_last,h->model_before.leaf,h->attempts,h->materials);
 state_text(line,&used,"model_pre",&h->model_before);state_text(line,&used,"model_post",&h->model_after);
 if(h->attempts){state_text(line,&used,"rigid_pre",&h->rigid_before);state_text(line,&used,"rigid_post",&h->rigid_after);append(line,sizeof line,&used," matrix_arg=%08X",h->matrix_arg);array(line,sizeof line,&used,"argument_matrix",h->matrix,16);}
 array(line,sizeof line,&used,"material_context",h->material_ctx,2);array(line,sizeof line,&used,"material_c4",h->material_c4,2);
 array(line,sizeof line,&used,"material_this",h->material_this,2);array(line,sizeof line,&used,"material_enabled7D_leaf81",h->material_enabled7d,2);
 array(line,sizeof line,&used,"material_argument_context",h->material_arg_ctx,2);array(line,sizeof line,&used,"material_context6C",h->material_ctx6c,2);
 fprintf(stderr,"%s\n",line);
}
static void manager_before(HptManager*m,const VabGuestCpu*cpu){
 m->previous=manager_scope;m->manager=cpu->gpr[1];m->pass=argument(cpu,1);m->caller=argument(cpu,0);
 m->depth=m->previous?m->previous->depth+1:1;
 manager_scope=m;
 if(atomic_load_explicit(&managers,memory_order_relaxed)>=TOPSPIN_HAIR_PASS_MANAGER_LIMIT)return;
 if(m->pass>1||m->depth>HPT_DEPTH||!valid(m->manager,0x11c))return;
 m->counter=word(0x30dbc8);if(m->counter<minimum_counter)return;
 uint32_t table=word(m->manager+0xa0);m->tier_count=word(m->manager+0xa4);
 if(m->tier_count>HPT_TIERS||!valid(table,(size_t)m->tier_count*4)){m->invalid=1;return;}
 for(uint32_t i=0;i<m->tier_count;i++)m->tier_heads+=model_heads(word(table+i*4),NULL);
 if(!m->tier_heads||!reserve(&managers,TOPSPIN_HAIR_PASS_MANAGER_LIMIT,&m->sequence))return;
 m->manager_mode=word(m->manager+0xc0);m->manager_dirty=byte(m->manager+0x95);m->manager_counter=word(m->manager+0x118);
 m->selector_first=m->selector_last=UINT32_MAX;m->lod_global=word(0x2fc59c);m->thresholds=word(m->manager+0xb0);m->threshold_count=word(m->manager+0xb4);
 m->context_before=context_state(&m->c4_before,&m->ctx_mode_before);
 m->admitted=1;
}
static void manager_after(HptManager*m){
 if(manager_scope!=m)return;
 if(m->admitted){uint32_t c4,mode,context=context_state(&c4,&mode);
  char line[HPT_LINE];size_t used=0;
  append(line,sizeof line,&used,"[HAIR-PASS] summary=%u author=djultra64 counter=%u manager=%08X requested_pass=%u caller=%08X tiers=%u tier_heads=%u managerC0=%u manager95=%u manager118=%u>%u ctx=%08X:%u:%u>%08X:%u:%u global2FC59C=%u thresholds=%08X/%u selector_count=%u selected_first=%u selected_last=%u",m->sequence,m->counter,m->manager,m->pass,m->caller,m->tier_count,m->tier_heads,m->manager_mode,m->manager_dirty,m->manager_counter,word(m->manager+0x118),m->context_before,m->c4_before,m->ctx_mode_before,context,c4,mode,m->lod_global,m->thresholds,m->threshold_count,m->selector_count,m->selector_first,m->selector_last);
  array(line,sizeof line,&used,"selector_callers",m->selector_callers,4);array(line,sizeof line,&used,"selector_values",m->selector_values,4);
  append(line,sizeof line,&used," model_calls=%u heads=%u rigid=%u material=%u model_limit=%u summary_truncated=%u%s",m->models,m->heads,m->rigids,m->materials,m->model_limit,(unsigned)(m->summary_used>=sizeof m->summary-1),m->summary);
  fprintf(stderr,"%s\n",line);
 }
 manager_scope=m->previous;
}
static void model_before(HptModel*m,const VabGuestCpu*cpu){
 m->previous=model_scope;m->manager=manager_scope;m->model=cpu->gpr[1];m->caller=argument(cpu,0);m->pass=argument(cpu,3);
 model_scope=m;
 if(!m->manager||!m->manager->admitted||m->pass>1)return;
 if(m->manager->models>=HPT_MODELS){m->manager->model_limit=1;return;}
 m->tier_mask=tier_mask(m->manager->manager,m->model);if(!m->tier_mask)return;
 m->lod=UINT32_MAX;if(!(m->tier_mask&(m->tier_mask-1)))for(uint32_t i=0;i<HPT_TIERS;i++)if(m->tier_mask==(1u<<i)){m->lod=i;break;}
 model_heads(m->model,m);if(!m->head_count)return;
 m->context_before=context_state(&m->c4_before,&m->mode_before);m->manager->models++;m->manager->heads+=m->head_count;m->admitted=1;
}
static void model_after(HptModel*m){
 if(model_scope!=m)return;
 if(!m->admitted){model_scope=m->previous;return;}
 HptManager*owner=m->manager;uint32_t c4,mode,context=context_state(&c4,&mode);
 append(owner->summary,sizeof owner->summary,&owner->summary_used," model=%08X/pass%u/unique_lod%u/tier_mask%02X/caller%08X/ctx%08X:%u:%u>%08X:%u:%u/trunc%u",m->model,m->pass,m->lod,m->tier_mask,m->caller,m->context_before,m->c4_before,m->mode_before,context,c4,mode,m->truncated);
 for(unsigned i=0;i<m->head_count;i++){
  HptHead*h=&m->head[i];snapshot(&h->model_after,h->model_before.leaf);
  append(owner->summary,sizeof owner->summary,&owner->summary_used," head=%08X/vis%u>%u/flags%02X/hidden%u>%u/mode%u>%u/bypass%u/rigid%u/material%u",h->model_before.leaf,h->model_before.visible,h->model_after.visible,h->model_before.flags,h->model_before.hidden,h->model_after.hidden,h->model_before.mode,h->model_after.mode,h->model_before.bypass,h->attempts,h->materials);
  detail(m,h);
 }
 model_scope=m->previous;
}
static void rigid_before(HptRigid*r,const VabGuestCpu*cpu){
 r->previous=rigid_scope;r->model=model_scope;rigid_scope=r;if(!r->model||!r->model->admitted||r->model->manager!=manager_scope)return;
 uint32_t leaf=cpu->gpr[1];if(!head_leaf(leaf))return;
 for(unsigned i=0;i<r->model->head_count;i++){
  HptHead*h=&r->model->head[i];if(h->model_before.leaf!=leaf)continue;
  r->head=h;h->attempts++;r->model->manager->rigids++;snapshot(&h->rigid_before,leaf);
  h->matrix_arg=argument(cpu,1);read_bytes(h->matrix_arg,h->matrix,sizeof h->matrix);rigid_scope=r;break;
 }
}
static void rigid_after(HptRigid*r){if(rigid_scope==r){if(r->head)snapshot(&r->head->rigid_after,r->head->model_before.leaf);rigid_scope=r->previous;}}
static void material_before(const VabGuestCpu*cpu){
 HptRigid*r=rigid_scope;if(!r||!r->head||r->model!=model_scope||r->model->manager!=manager_scope||cpu->gpr[1]!=r->head->model_before.leaf+4)return;
 HptHead*h=r->head;unsigned index=h->materials++;r->model->manager->materials++;
 if(index<2){
  uint32_t mode;h->material_ctx[index]=context_state(&h->material_c4[index],&mode);
  /* 68AE0 receives the embedded drawable at leaf+4. Its byte+7D is the
   * same address as owning rigid leaf+81; its first argument is the actual
   * renderer/context whose dword+6C rejects modes1 and3 in the original. */
  h->material_this[index]=cpu->gpr[1];h->material_enabled7d[index]=byte(cpu->gpr[1]+0x7d);
  h->material_arg_ctx[index]=argument(cpu,1);
  h->material_ctx6c[index]=valid(h->material_arg_ctx[index],0x70)?word(h->material_arg_ctx[index]+0x6c):UINT32_MAX;
 }
}

/* Every host observation, including getenv, stdio and TLS bookkeeping, is
 * between full CPU/fenv guards. Originals run once outside those guards. */
#define HPT_START() VabGuestCpu cpu;vab_cpu_save(&cpu);int on=enabled()
#define HPT_ORIGINAL(original) vab_cpu_restore(&cpu);original();vab_cpu_save(&cpu)
extern void __real_sub_00079AE0(void),__real_sub_00085450(void),__real_sub_0006BCA0(void),__real_sub_00086810(void),sub_00068AE0_hair_trace_original(void);
#define HPT_OFF(original) if(atomic_load_explicit(&configured,memory_order_acquire)==2){original();return;}
void __wrap_sub_00079AE0(void){HPT_OFF(__real_sub_00079AE0);HPT_START();if(!on){vab_cpu_restore(&cpu);__real_sub_00079AE0();return;}HptManager m={0};manager_before(&m,&cpu);HPT_ORIGINAL(__real_sub_00079AE0);manager_after(&m);vab_cpu_restore(&cpu);}
void __wrap_sub_00085450(void){HPT_OFF(__real_sub_00085450);HPT_START();if(!on){vab_cpu_restore(&cpu);__real_sub_00085450();return;}HptModel m={0};model_before(&m,&cpu);HPT_ORIGINAL(__real_sub_00085450);model_after(&m);vab_cpu_restore(&cpu);}
void __wrap_sub_0006BCA0(void){HPT_OFF(__real_sub_0006BCA0);HPT_START();if(!on){vab_cpu_restore(&cpu);__real_sub_0006BCA0();return;}HptRigid r={0};rigid_before(&r,&cpu);HPT_ORIGINAL(__real_sub_0006BCA0);rigid_after(&r);vab_cpu_restore(&cpu);}
void sub_00068AE0(void){HPT_OFF(sub_00068AE0_hair_trace_original);HPT_START();if(on)material_before(&cpu);HPT_ORIGINAL(sub_00068AE0_hair_trace_original);vab_cpu_restore(&cpu);}
void __wrap_sub_00086810(void){
 HPT_OFF(__real_sub_00086810);HPT_START();HptManager*m=on?manager_scope:NULL;uint32_t caller=0;int matching=0;
 if(m&&m->admitted&&argument(&cpu,1)==m->manager+0x10&&argument(&cpu,2)==m->thresholds&&argument(&cpu,3)==m->threshold_count){matching=1;caller=argument(&cpu,0);}
 HPT_ORIGINAL(__real_sub_00086810);
 if(matching){uint32_t value=cpu.gpr[0];unsigned index=m->selector_count++;if(!index)m->selector_first=value;m->selector_last=value;if(index<4){m->selector_values[index]=value;m->selector_callers[index]=caller;}}
 vab_cpu_restore(&cpu);
}
