/* Original MainMenu-only private chunk/focus adapter. Own work: djultra64. */
#include "menu_pc_entries.h"
#include "menu_pc_sha256.h"
#include "kernel/xbox_memory_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <fenv.h>
#include <stdarg.h>
extern _Thread_local uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,g_seh_ebp,g_fs_base,g_itail_site;
extern _Thread_local int g_df,g_fp_top,g_fp_cmp;
extern _Thread_local uint16_t g_fp_control_word,g_fp_cc;
extern _Thread_local double g_fp_stack[8];
extern _Thread_local RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
extern _Thread_local RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_00023F30_gen(void);
extern void sub_0002EF80_gen(void);
extern void sub_0011D4D0_gen(void);
extern void sub_00023780_gen(void);
extern void sub_00023880_gen(void);
extern void sub_0002E7B0_gen(void);
extern void sub_0009CC50_gen(void);
extern void __real_sub_000348E0(void);
extern void sub_00022C30(void),sub_00040B00(void),sub_00027820(void),sub_00020B80(void);
typedef struct {uint32_t r[12];int df,top,cmp;uint16_t cw,cc;double fp[8];RecompXmm x[8];RecompMmx m[8];fenv_t host;} Cpu;
static void cpu_save(Cpu*s){
 uint32_t r[]={g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,g_seh_ebp,g_fs_base,g_itail_site,0};memcpy(s->r,r,sizeof r);
 s->df=g_df;s->top=g_fp_top;s->cmp=g_fp_cmp;s->cw=g_fp_control_word;s->cc=g_fp_cc;memcpy(s->fp,g_fp_stack,sizeof s->fp);
 RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};memcpy(s->x,x,sizeof x);
 RecompMmx m[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};memcpy(s->m,m,sizeof m);fegetenv(&s->host);
}
static void cpu_restore(const Cpu*s){
 g_eax=s->r[0];g_ecx=s->r[1];g_edx=s->r[2];g_ebx=s->r[3];g_esi=s->r[4];g_edi=s->r[5];g_ebp=s->r[6];g_esp=s->r[7];g_seh_ebp=s->r[8];g_fs_base=s->r[9];g_itail_site=s->r[10];
 g_df=s->df;g_fp_top=s->top;g_fp_cmp=s->cmp;g_fp_control_word=s->cw;g_fp_cc=s->cc;memcpy(g_fp_stack,s->fp,sizeof s->fp);
 g_xmm0=s->x[0];g_xmm1=s->x[1];g_xmm2=s->x[2];g_xmm3=s->x[3];g_xmm4=s->x[4];g_xmm5=s->x[5];g_xmm6=s->x[6];g_xmm7=s->x[7];
 g_mm0=s->m[0];g_mm1=s->m[1];g_mm2=s->m[2];g_mm3=s->m[3];g_mm4=s->m[4];g_mm5=s->m[5];g_mm6=s->m[6];g_mm7=s->m[7];fesetenv(&s->host);
}
static struct {unsigned char *chunk[3];uint32_t bytes[3],va[3],scene,control[4],generation,font,size,observed_focus,observed_generation;int configured,installed[3],template_installed;_Atomic uint64_t request;_Atomic int ack;_Atomic uint32_t restore_generation;uint32_t host_generation;} pc;
/* Transition-only evidence. Never reads guest memory when disabled, and never
 * logs untrusted string/payload bytes. Four records/stage/generation (sixteen
 * for the scoped control-event stage), first
 * four scene generations only; configure is generation0. djultra64. */
enum BridgeStage {BR_SCENE,BR_RESOURCE,BR_COMPILE,BR_CONTROL,BR_CONTROL_CANDIDATE,BR_ACTIVATION,BR_RESTORE,BR_EVENT,BR_FOCUS,BR_FOCUS_STATE,BR_STAGES};
static unsigned bridge_counts[5][BR_STAGES];
static int bridge_enabled(void){static int enabled=-1;if(enabled<0){const char*e=getenv("TOPSPIN_VIDEO_TRACE");enabled=e&&!strcmp(e,"1");}return enabled;}
static int bridge_on(void){return bridge_enabled()&&pc.generation<=4;}
static void bridge_trace(enum BridgeStage stage,const char*fmt,...){
 if(!bridge_on()||bridge_counts[pc.generation][stage]>=((stage==BR_EVENT||stage==BR_FOCUS_STATE)?16u:stage==BR_FOCUS?32u:4u))return;
 static const char*names[]={"scene","resource","compile","control","control_candidate","activation","restore","event","focus","focus_state"};
 bridge_counts[pc.generation][stage]++;
 fprintf(stderr,"[MENU-PC-BRIDGE] gen=%u stage=%s ",pc.generation,names[stage]);
 va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);
}
static _Thread_local int resource_kind=-1,internal;
static _Thread_local uint32_t resource_lua;
#ifdef TOPSPIN_NATIVE_GRAPHICS
static void graphics_scene_reset(void);
static void graphics_register_control(uint32_t,const uint32_t[3]);
static void graphics_guest_poll(void);
static const char*graphics_label(uint32_t,char[40]);
#include "menu_graphics_resources.h"
#endif
static const char*original_sha[]={"6b839099983d0a740fa72cf610684efac9635ab2817126fc6e20c70f5d1f9abe","1699673e360194572e600268176a283606fdc4e92bd834d8ff665e08ebdc7d92","6fb70803608f36957b5e48612683fc45a573e303abba5a10738d4015ee3f7720"};
#ifndef TOPSPIN_NATIVE_GRAPHICS
static const char*private_sha[]={"0d5fd2a9fe10c32939e36f11e30168007fa3125e8f15c3ebc4c365cac2a726ed","a85e165618cbb31c0796eb8520a7daea5962d0e069ec6bd2be6f19b8477399d7","61f07b22fbd29e37833010a3399c4c9a41db7799dd0911577973210613777d6c"};
static const uint32_t private_size[]={91741,13726,9123};
static const uint64_t private_hash[]={UINT64_C(0xb3242aa17206d784),UINT64_C(0x344c4191028f5f31),UINT64_C(0xcefe4601a897f9bc)};
#endif
static const uint32_t original_size[]={114516,9478,7992};
static const uint64_t original_hash[]={UINT64_C(0x4fc78ecbe930d078),UINT64_C(0xd729d5f3cd3db51f),UINT64_C(0xa0b2ea8fbc4fd0f3)};
static void*map(uint32_t p,size_t n){if(p<0x10000u||(uint64_t)p+n>0x4000000u)return NULL;return(void*)((uintptr_t)g_xbox_mem_offset+p);}
static int rd(uint32_t p,void*out,size_t n){void*q=map(p,n);if(!q)return 0;memcpy(out,q,n);return 1;}
static uint32_t word(uint32_t p){uint32_t x=0;rd(p,&x,4);return x;}
static void put(uint32_t p,uint32_t x){void*q=map(p,4);if(q)memcpy(q,&x,4);}
static uint64_t fingerprint(const void*v,size_t n){const unsigned char*p=v;uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<n;i++)h=(h^p[i])*UINT64_C(1099511628211);return h;}
static int str_is(uint32_t obj,const char*s){uint32_t p=word(obj);size_t n=strlen(s);if(p<0x10010u||word(p-12)!=n)return 0;void*q=map(p,n+1);return q&&!memcmp(q,s,n+1);}
static int resource_name(uint32_t obj,int kind){
 uint32_t p=word(obj);if(p<0x10010u)return 0;uint32_t n=word(p-12);if(!n||n>512)return 0;const char*q=map(p,(size_t)n+1);if(!q||q[n])return 0;const char*base=q;for(uint32_t i=0;i<n;i++)if(q[i]=='\\'||q[i]=='/')base=q+i+1;return !strcmp(base,kind==2?"main_optionrules.sceneData":kind?"newmain.scene":"newmain.sceneData");
}
static int newmain(uint32_t scene){return scene==pc.scene&&pc.installed[0]&&pc.installed[1]&&pc.installed[2]&&map(scene,0x1a0)&&str_is(scene+0x18c,"newmain");}
static uint32_t current_focus(uint32_t scene){return map(scene,0x178)?word(word(scene+0x174)):0;}
static int control_valid(uint32_t c,uint32_t scene){return map(c,0x3c)&&word(c)==0x1e671cu&&word(c+0xc)==scene;}
/* Observe original focus state, shared by both setter and frame paths. The
 * actual title can activate controls through23270/237D0 without calling23780.
 * Requests are edges of the exact private control, not repeated frame states.
 * No guest input, control state, timing or action is synthesized here. */
static int observe_pc_focus(int from_poll){
 uint32_t scene=pc.scene;
 if(!pc.configured||!newmain(scene))return 0;
 uint32_t manager=word(0x2b969c);
 if(!map(manager,0x1248)||word(manager+0x1244)!=scene)return 0;
 uint32_t focus=current_focus(scene);
 if(pc.observed_generation==pc.generation&&pc.observed_focus==focus)return 0;
 pc.observed_generation=pc.generation;pc.observed_focus=focus;
 int which=focus==pc.control[1]?1:focus==pc.control[2]?2:0;
#ifdef TOPSPIN_NATIVE_GRAPHICS
 if(which==1)return 0; /* v3 uses a real RUI page, never a host modal. */
#endif
 if(!which||!control_valid(focus,scene))return 0;
 uint64_t request=(uint64_t)pc.generation<<32|(which==1?TOPSPIN_MENU_PC_OPEN_GRAPHICS:TOPSPIN_MENU_PC_QUIT),expected=0;
 int emitted=which==2?(atomic_store(&pc.request,request),1):atomic_compare_exchange_strong(&pc.request,&expected,request);
 if(emitted&&from_poll)bridge_trace(BR_ACTIVATION,"source=focus_poll name=%s scene=%08X focus=%08X emitted=1 installed=%d%d%d",which==1?"PcGraphicsPanel":"PcQuitRequest",scene,focus,pc.installed[0],pc.installed[1],pc.installed[2]);
 return emitted?which:0;
}
static uint32_t allocate(uint32_t bytes){uint32_t p=xbox_ContiguousAlloc(bytes,16);if(p<0x80010000u||(uint64_t)p+bytes>UINT64_C(0x84000000))return 0;return p&0x3ffffffu;}
int topspin_menu_pc_configure(const char*dir){
 if(pc.configured||!dir||strlen(dir)>800)return 0;
 const char*names[]={"newmain.sceneData","newmain.scene","main_optionrules.sceneData"};unsigned char*data[3]={0};int good=1;
 for(unsigned i=0;i<3;i++){
  char path[1024];if(snprintf(path,sizeof path,"%s/%s",dir,names[i])>=(int)sizeof path){good=0;break;}
  FILE*f=fopen(path,"rb");if(!f){good=0;break;}
  data[i]=malloc(private_size[i]);if(!data[i]){fclose(f);good=0;break;}
  if(fread(data[i],1,private_size[i],f)!=private_size[i]||fgetc(f)!=EOF||ferror(f)||fingerprint(data[i],private_size[i])!=private_hash[i]||!menu_pc_sha_is(data[i],private_size[i],private_sha[i]))good=0;
  if(fclose(f))good=0;
  if(!good)break;
 }
 if(!good){free(data[0]);free(data[1]);free(data[2]);return 0;}
 for(unsigned i=0;i<3;i++){pc.chunk[i]=data[i];pc.bytes[i]=private_size[i];}pc.configured=1;bridge_trace(BR_SCENE,"action=configure configured=1 installed=000");return 1;
}
TopspinMenuPcRequest topspin_menu_pc_take_host_request(void){uint64_t r=atomic_exchange(&pc.request,0);if(r)pc.host_generation=(uint32_t)(r>>32);return(TopspinMenuPcRequest)(uint32_t)r;}
void topspin_menu_pc_request_focus_restore(void){atomic_store(&pc.restore_generation,pc.host_generation);}
int topspin_menu_pc_take_focus_restored(void){return atomic_exchange(&pc.ack,0);}
void sub_00023F30(void){
 uint32_t args[2];
 if(pc.configured&&rd(g_esp,args,sizeof args)&&str_is(args[1],"newmain")&&map(g_ecx,0x1a0)){
  pc.scene=g_ecx;pc.generation++;pc.installed[0]=pc.installed[1]=0;pc.installed[2]=pc.template_installed;pc.font=pc.size=0;memset(pc.control,0,sizeof pc.control);
#ifdef TOPSPIN_NATIVE_GRAPHICS
  graphics_scene_reset();
#endif
  bridge_trace(BR_SCENE,"action=newmain scene=%08X installed=%d%d%d",pc.scene,pc.installed[0],pc.installed[1],pc.installed[2]);
 }
 sub_00023F30_gen();
}
void sub_0002EF80(void){
 int previous=resource_kind;uint32_t old_lua=resource_lua,args[4]={0};resource_kind=-1;resource_lua=0;
 int candidate=-1,name_ok=0;const char*reason="scope";
 if(pc.configured&&rd(g_esp,args,sizeof args)){
  if(args[0]==0x23fa7u)candidate=resource_kind=0;
  else if(args[0]==0x24091u)candidate=resource_kind=1;
  else if(args[0]==0x3f1f5u)candidate=resource_kind=2;
  /* Templates are compiled into the original global resource cache before
   * newmain exists. Exact caller/name/body hashes scope this replacement;
   * scene ownership cannot be required at that earlier lifecycle point. */
  if(resource_kind>=0){name_ok=resource_name(args[2],resource_kind);if(name_ok){resource_lua=args[1];reason="matched";if(resource_kind==2){pc.template_installed=0;pc.installed[2]=0;}}else {resource_kind=-1;reason="name";}}
 }
 sub_0002EF80_gen();
 if(candidate>=0)bridge_trace(BR_RESOURCE,"kind=%d caller=%08X name_match=%d reason=%s status=%08X installed=%d%d%d",candidate,args[0],name_ok,reason,g_eax,pc.installed[0],pc.installed[1],pc.installed[2]);
 resource_kind=previous;resource_lua=old_lua;
}
void sub_0011D4D0(void){
 uint32_t args[4]={0},sp=g_esp;int applied=0,k=resource_kind;const char*reason="kind";
 if(k>=0&&k<3){
  reason="configured";
  if(pc.configured){reason="context";
   if(g_ecx==resource_lua){reason="stack";
    if(rd(sp,args,sizeof args)){reason="caller";
     if(args[0]==0x2f0ceu){reason="size";
      if(args[2]==original_size[k]){void*p=map(args[1],args[2]);reason="range";
       if(p){reason="hash";
        if(fingerprint(p,args[2])==original_hash[k]&&menu_pc_sha_is(p,args[2],original_sha[k])){
         reason="allocation";for(unsigned i=0;i<3;i++)if(!pc.va[i])pc.va[i]=allocate(pc.bytes[i]);
         if(pc.va[0]&&pc.va[1]&&pc.va[2]){memcpy(map(pc.va[k],pc.bytes[k]),pc.chunk[k],pc.bytes[k]);put(sp+4,pc.va[k]);put(sp+8,pc.bytes[k]);applied=1;reason="applied";}
        }
       }
      }
     }
    }
   }
  }
 }
 sub_0011D4D0_gen();
 if(applied){put(sp+4,args[1]);put(sp+8,args[2]);if(g_eax==0){pc.installed[k]=1;if(k==2)pc.template_installed=1;}}
 if(k>=0&&k<3)bridge_trace(BR_COMPILE,"kind=%d caller=%08X applied=%d reason=%s status=%08X installed=%d%d%d",k,args[0],applied,reason,g_eax,pc.installed[0],pc.installed[1],pc.installed[2]);
}
void sub_00023880(void){
 uint32_t args[3]={0},scene=g_ecx;int known=pc.configured&&newmain(scene)&&rd(g_esp,args,sizeof args);int which=-1,candidate=-1;
 const char*names[]={"PcGraphicsOptions","PcGraphicsPanel","PcQuitRequest","PcQuitGame"};
 if(known){for(int i=0;i<4;i++)if(str_is(args[2],names[i]))which=candidate=i;}
 /* Diagnostic-only candidate discovery intentionally precedes the readiness
  * gate. Keep production registration subject to the unchanged gate. */
 else if(bridge_on()&&rd(g_esp,args,sizeof args))for(int i=0;i<4;i++)if(str_is(args[2],names[i]))candidate=i;
 sub_00023880_gen();
 int registered=0;uint32_t c=0;
 if(which>=0){c=word(args[1]);if(control_valid(c,scene)){pc.control[which]=c;registered=1;}}
#ifdef TOPSPIN_NATIVE_GRAPHICS
 if(known)graphics_register_control(scene,args);
#endif
 if(candidate>=0&&bridge_on()){
  if(!c)c=word(args[1]);
  int valid=control_valid(c,scene);
  bridge_trace(known?BR_CONTROL:BR_CONTROL_CANDIDATE,"name=%s scene=%08X ready=%d control=%08X class=%08X owner=%08X valid=%d registered=%d installed=%d%d%d",names[candidate],scene,known,c,word(c),word(c+12),valid,registered,pc.installed[0],pc.installed[1],pc.installed[2]);
 }
}
/* Hash only a validated, bounded original RichString; no opaque text IO. */
static uint64_t trace_string_hash(uint32_t object,uint32_t*length){
 uint32_t p=word(object);*length=0;if(p<0x10010)return 0;
 uint32_t n=word(p-12);if(!n||n>128)return 0;
 const unsigned char*data=map(p,(size_t)n+1);if(!data||data[n])return 0;
 *length=n;return fingerprint(data,n);
}
void sub_00023780(void){
 uint32_t args[3]={0},scene=g_ecx,before=current_focus(scene),generation=pc.generation;int candidate=-1,ready=0;
 uint32_t request_len=0,before_len=0;uint64_t request_hash=0,before_hash=0;
 int focus_trace=bridge_on()&&bridge_counts[pc.generation][BR_FOCUS]<32&&!internal&&pc.configured&&scene==pc.scene&&newmain(scene)&&rd(g_esp,args,sizeof args);
 if(focus_trace){request_hash=trace_string_hash(args[1],&request_len);if(control_valid(before,scene))before_hash=trace_string_hash(before+8,&before_len);}

 if(!internal&&pc.configured&&scene==pc.scene&&newmain(scene)&&rd(g_esp,args,sizeof args)&&args[2]==0){
  ready=1;if(str_is(args[1],"PcGraphicsPanel"))candidate=1;
  else if(str_is(args[1],"PcQuitRequest"))candidate=2;
 }
 if(candidate<0&&bridge_on()&&rd(g_esp,args,sizeof args)){
  if(str_is(args[1],"PcGraphicsOptions"))candidate=0;else if(str_is(args[1],"PcGraphicsPanel"))candidate=1;else if(str_is(args[1],"PcQuitRequest"))candidate=2;
 }
 sub_00023780_gen();int emitted=0;
 if(focus_trace&&generation==pc.generation){
  uint32_t after=current_focus(scene),after_len=0;uint64_t after_hash=control_valid(after,scene)?trace_string_hash(after+8,&after_len):0;
  bridge_trace(BR_FOCUS,"caller=%08X player=%u request_obj=%08X request_data=%08X request_len=%u request_hash=%016llX before=%08X before_len=%u before_hash=%016llX after=%08X after_len=%u after_hash=%016llX",args[0],args[2],args[1],word(args[1]),request_len,(unsigned long long)request_hash,before,before_len,(unsigned long long)before_hash,after,after_len,(unsigned long long)after_hash);
 }

 if(!internal&&generation==pc.generation&&scene==pc.scene)emitted=observe_pc_focus(0)!=0;
 if(candidate>=0&&bridge_on())bridge_trace(BR_ACTIVATION,"name=%s caller=%08X player=%u scene=%08X ready=%d before=%08X focus=%08X cached=%08X emitted=%d installed=%d%d%d",candidate==0?"PcGraphicsOptions":candidate==1?"PcGraphicsPanel":"PcQuitRequest",args[0],args[2],scene,ready,before,current_focus(scene),pc.control[candidate],emitted,pc.installed[0],pc.installed[1],pc.installed[2]);
}
/* Original RGui control event dispatch, not an input replacement. Event codes:
 * 1 AButton,2 Start,3 Up,4 Down,5 BButton,6 Back. Original348E0 passes its
 * RichString argument to34890 then34580; caller-live guest state is untouched.
 * Capture before original: original may change ECX, focus and scene lifetime.
 * No guest-memory reads and no extra IO when the existing trace gate is OFF. */
void __wrap_sub_000348E0(void){
 uint32_t args[2]={0},control=g_ecx,scene=0,before=0,generation=pc.generation;
 uint32_t state[8]={0};int which=-1,event=0;
 if(bridge_on()&&bridge_counts[pc.generation][BR_EVENT]<16&&pc.configured&&
    control_valid(control,pc.scene)&&newmain(pc.scene)&&rd(g_esp,args,sizeof args)){
  scene=pc.scene;
  for(int i=0;i<4;i++)if(control==pc.control[i])which=i;
  if(which<0&&str_is(control+8,"group_pc_graphics"))which=4;
  if(which<0&&str_is(control+8,"group_pc_graphics|SelectedText"))which=5;
  if(which>=0){
   const char*names[]={"AButton","Start","Up","Down","BButton","Back"};
   for(int i=0;i<6;i++)if(str_is(args[1],names[i]))event=i+1;
   if(event){
    before=current_focus(scene);state[0]=word(control+0x10);state[1]=word(control+0x14);state[2]=word(control+0x18);
    state[3]=word(control+0x2c);state[4]=word(state[3]);state[5]=word(control+0x30);state[6]=word(control+0x34);state[7]=word(control+0x28);
   }
  }
 }
 __real_sub_000348E0();
 if(event&&generation==pc.generation)
  bridge_trace(BR_EVENT,"control=%d event=%d caller=%08X object=%08X scene=%08X before=%08X after=%08X bindings=%08X/%08X/%08X node=%08X class=%08X context=%08X context_flags=%08X action_before=%08X action_after=%08X",which,event,args[0],control,scene,before,current_focus(scene),state[0],state[1],state[2],state[3],state[4],state[5],state[6],state[7],word(control+0x28));
}
/* Helpers use the current guest stack below ESP, and preserve all caller-live
 * bytes in the explicit scratch window. Callee frames grow below that window. */
static int scratch(Cpu*s,uint32_t*base,unsigned char saved[256]){cpu_save(s);if(g_esp<0x10200u)return 0;*base=g_esp-256u;return rd(*base,saved,256);}
static void unscratch(Cpu*s,uint32_t base,const unsigned char saved[256]){memcpy(map(base,256),saved,256);cpu_restore(s);}
static void call0(void(*fn)(void),uint32_t sp,uint32_t context,uint32_t a,uint32_t b){put(sp,0);put(sp+4,a);put(sp+8,b);g_esp=sp;g_ecx=context;fn();}
#ifdef TOPSPIN_NATIVE_GRAPHICS
#include "menu_graphics_bridge.inc"
#endif
void topspin_menu_pc_guest_poll(void){
 observe_pc_focus(1);
#ifdef TOPSPIN_NATIVE_GRAPHICS
 graphics_guest_poll();
#endif
 uint32_t generation=atomic_exchange(&pc.restore_generation,0);if(!generation)return;
 if(generation!=pc.generation){atomic_store(&pc.ack,1);bridge_trace(BR_RESTORE,"requested_gen=%u action=stale ack=1",generation);return;}
 uint32_t scene=pc.scene;
 if(newmain(scene)&&control_valid(pc.control[0],scene)&&control_valid(pc.control[1],scene)&&current_focus(scene)==pc.control[1]){
  Cpu cpu;uint32_t base;unsigned char saved[256];
  if(!scratch(&cpu,&base,saved)){atomic_store(&pc.restore_generation,generation);bridge_trace(BR_RESTORE,"requested_gen=%u action=scratch_retry ack=0",generation);return;}
  const char text[]="PcGraphicsOptions";memcpy(map(base+160,sizeof text),text,sizeof text);
  call0(sub_00022C30,base+96,base+144,base+160,0);
  internal++;call0(sub_00023780_gen,base+96,scene,base+144,0);internal--;
  call0(sub_00020B80,base+96,base+144,0,0);
  int restored=current_focus(scene)==pc.control[0];unscratch(&cpu,base,saved);
  if(!restored){atomic_store(&pc.restore_generation,generation);bridge_trace(BR_RESTORE,"requested_gen=%u action=focus_retry ack=0",generation);return;}
 }
 observe_pc_focus(0);
 atomic_store(&pc.ack,1);bridge_trace(BR_RESTORE,"requested_gen=%u action=complete focus=%08X ack=1",generation,current_focus(scene));
}
/* Some original navigation paths use23270/237D0 directly. Observe the actual
 * scene/focus predicates at the existing guest frame boundary, only on changes.
 * 40E50 uses manager[2B969C]+1244 as its default scene; all four player focus
 * slots are captured so a different active port is not silently excluded. */
static void trace_focus_state(void){
 static uint32_t last[11];static int have;
 if(!bridge_on()||bridge_counts[pc.generation][BR_FOCUS_STATE]>=16||!pc.configured||!newmain(pc.scene))return;
 uint32_t state[11]={pc.generation,pc.scene,0,0,0,0,0,0,0,0,0};
 rd(word(pc.scene+0x174),state+2,16);
 uint32_t manager=word(0x2b969c),active=map(manager,0x1248)?word(manager+0x1244):0;
 state[6]=active;int active_newmain=map(active,0x1a0)&&str_is(active+0x18c,"newmain");
 if(active_newmain)rd(word(active+0x174),state+7,16);
 if(have&&!memcmp(last,state,sizeof state))return;
 memcpy(last,state,sizeof state);have=1;
 uint32_t pc_len=0,active_len=0;uint64_t pc_hash=control_valid(state[2],pc.scene)?trace_string_hash(state[2]+8,&pc_len):0;
 uint64_t active_hash=active_newmain&&control_valid(state[7],active)?trace_string_hash(state[7]+8,&active_len):0;
 bridge_trace(BR_FOCUS_STATE,"pc_scene=%08X pc_focus=%08X/%08X/%08X/%08X pc_len=%u pc_hash=%016llX manager=%08X active_scene=%08X active_equals_pc=%d active_is_newmain=%d active_focus=%08X/%08X/%08X/%08X active_len=%u active_hash=%016llX",pc.scene,state[2],state[3],state[4],state[5],pc_len,(unsigned long long)pc_hash,manager,active,active==pc.scene,active_newmain,state[7],state[8],state[9],state[10],active_len,(unsigned long long)active_hash);
}
void sub_0009CC50(void){topspin_menu_pc_guest_poll();sub_0009CC50_gen();trace_focus_state();}
void sub_0002E7B0(void){
 uint32_t args[4],sp=g_esp;int label=-1,source=0;
#ifdef TOPSPIN_NATIVE_GRAPHICS
 char dynamic[40];const char*native_label=NULL;
#endif
 if(pc.configured&&pc.installed[0]&&rd(sp,args,sizeof args)){
  if(str_is(args[1],"pc_graphics_options"))label=0;
  else if(str_is(args[1],"pc_quit_game"))label=1;
  else if(str_is(args[1],"main_options"))source=1;
#ifdef TOPSPIN_NATIVE_GRAPHICS
  native_label=graphics_label(args[1],dynamic);
#endif
 }
 sub_0002E7B0_gen();
 if(source&&map(args[2],12)&&(g_eax&255u)){
  uint32_t font=word(args[2]+4),size=word(args[2]+8);float scale;memcpy(&scale,&size,4);if(map(font,4)&&scale>0.f&&scale<1024.f){pc.font=font;pc.size=size;}
 }
 if((label>=0
#ifdef TOPSPIN_NATIVE_GRAPHICS
     ||native_label
#endif
    )&&pc.font&&map(pc.font,4)&&map(args[2],12)){
  Cpu cpu;uint32_t base;unsigned char saved[256];if(!scratch(&cpu,&base,saved))return;
  if((uint64_t)args[2]+12>base&&args[2]<(uint64_t)base+256)return;
  const char*text=label==0?"Graphics Options":"QUIT GAME";
#ifdef TOPSPIN_NATIVE_GRAPHICS
  if(native_label)text=native_label;
#endif
  size_t n=strlen(text);if(n>39){unscratch(&cpu,base,saved);return;}uint16_t utf[40]={0};for(size_t i=0;i<n;i++)utf[i]=(uint8_t)text[i];
  memcpy(map(base+176,(n+1)*2),utf,(n+1)*2);
  call0(sub_00040B00,base+96,base+144,base+176,(uint32_t)n);
  call0(sub_00027820,base+96,args[2],base+144,0);
  call0(sub_00020B80,base+96,base+144,0,0);
  put(args[2]+4,pc.font);put(args[2]+8,pc.size);
  unscratch(&cpu,base,saved);g_eax=(g_eax&0xffffff00u)|1u;
 }
}
