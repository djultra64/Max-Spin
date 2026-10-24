#include "sprite_ui_admission.h"
/* Verified per-image RUI resource/bind adapter. Own work: djultra64.
 * No original header, data, ABC metric or file is modified. */
#include "menu_prompt_runtime.h"
#include "controller_rui_resources.h"
#include "controls.h"
#include "input/input_host.h"
#include "kernel/xbox_memory_layout.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
extern _Thread_local uint32_t g_eax,g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
enum {MAX_TEMPLATES=8,MAX_RESOURCES=128,MAX_VARIANTS=32};
typedef struct {uint8_t*abc,*xpr;size_t abc_bytes,xpr_bytes;int sprite;} Template;
typedef struct {uint32_t object,header;unsigned source;} Resource;
typedef struct {unsigned source;uint8_t labels[9];uint32_t header;} Variant;
static struct {TopSpinMenuPromptPlatform platform;Template source[MAX_TEMPLATES];Resource resource[MAX_RESOURCES];Variant variant[MAX_VARIANTS];unsigned sources,variants;size_t pinned;_Atomic int configured,failed;int unsupported;} state;
static CRITICAL_SECTION runtime_lock;
static uint32_t resource_vptr=0x1fc6fc;
static size_t guest_budget=TOPSPIN_MENU_PROMPT_BUDGET;
static int success_trace;
static int rotate_reader_verified;
static unsigned association_logs,upload_logs,hud_logs;
static uint32_t word(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static void store(void*p,uint32_t v){memcpy(p,&v,4);}
static void*default_map(void*u,uint32_t va,size_t n){
 (void)u;uint64_t end=(uint64_t)va+n;
 if(!n||!((va>=0x10000&&end<=0x4000000)||(va>=0x80000000u&&end<=0x84000000u)))return NULL;
 return(void*)((uintptr_t)g_xbox_mem_offset+va);
}
static uint32_t default_alloc(void*u,uint32_t bytes,uint32_t align){(void)u;return xbox_ContiguousAlloc(bytes,align);}
int topspin_prompt_labels(const InputDeviceIdentity*identity,const uint8_t sources[6],uint8_t labels[6]){
 if(!identity||!sources||!labels)return -1;
 if(identity->kind==INPUT_DEVICE_NONE)return 0;
 uint8_t family[6],resolved[6];
 if(identity->kind!=INPUT_DEVICE_PAD||!topspin_rui_family_labels(identity->family,family))return -1;
 for(unsigned n=0;n<6;n++){
  unsigned source=sources[n];
  if(source>=INPUT_SRC_SOUTH&&source<=INPUT_SRC_NORTH){
   unsigned position=source-INPUT_SRC_SOUTH;
   unsigned face=identity->face_label[position];
   if(!face)face=input_family_face_label(identity->family,position);
   resolved[n]=topspin_rui_face_glyph(identity->family,face);
  }else if(source==INPUT_SRC_LEFT_SHOULDER)resolved[n]=family[5];
  else if(source==INPUT_SRC_RIGHT_SHOULDER)resolved[n]=family[4];
  else return -1;
  if(!resolved[n])return -1;
 }
 memcpy(labels,resolved,6);return 1;
}
static int default_labels(void*u,uint8_t labels[6]){
 (void)u;InputDeviceIdentity identity;uint8_t sources[6];
 int active=xbox_HostInputLastDevice(&identity);
 if(!active||identity.kind==INPUT_DEVICE_NONE){
  /* The SDK still supplies an explicit manual family in a NONE snapshot.
   * Resolve that preference locally; do not fabricate host input activity.
   * AUTO/NONE has UNKNOWN family, and keyboard snapshots never enter here. */
  if(identity.kind!=INPUT_DEVICE_NONE ||
     (identity.family!=INPUT_FAMILY_XBOX&&identity.family!=INPUT_FAMILY_PLAYSTATION&&identity.family!=INPUT_FAMILY_NINTENDO))return 0;
  identity.kind=INPUT_DEVICE_PAD;
 }
 if(!topspin_controls_guest_sources(sources))return -1;
 return topspin_prompt_labels(&identity,sources,labels);
}
int topspin_prompt_labels9(const InputDeviceIdentity*identity,const uint8_t sources[9],uint8_t labels[9]){
 if(!identity||!sources||!labels)return -1;if(identity->kind==INPUT_DEVICE_NONE)return 0;
 if(identity->kind!=INPUT_DEVICE_PAD)return -1;uint8_t result[9];
 unsigned base=identity->family==INPUT_FAMILY_XBOX?19:identity->family==INPUT_FAMILY_PLAYSTATION?22:identity->family==INPUT_FAMILY_NINTENDO?25:0;
 if(!base||sources[6]!=INPUT_STICK_RIGHT)return -1;result[6]=(uint8_t)base;
 for(unsigned n=0;n<9;n++){
  if(n==6)continue;unsigned source=sources[n];if(source==INPUT_SRC_BACK)result[n]=(uint8_t)(base+1);
  else if(source==INPUT_SRC_START)result[n]=(uint8_t)(base+2);
  else{uint8_t repeated[6],resolved[6];memset(repeated,source,6);if(topspin_prompt_labels(identity,repeated,resolved)!=1)return -1;result[n]=resolved[0];}
 }
 memcpy(labels,result,9);return 1;
}
static int default_labels9(void*u,uint8_t labels[9]){
 (void)u;InputDeviceIdentity identity;uint8_t sources[9];int active=xbox_HostInputLastDevice(&identity);
 if(!active||identity.kind==INPUT_DEVICE_NONE){
  if(identity.kind!=INPUT_DEVICE_NONE||(identity.family!=INPUT_FAMILY_XBOX&&identity.family!=INPUT_FAMILY_PLAYSTATION&&identity.family!=INPUT_FAMILY_NINTENDO))return 0;
  identity.kind=INPUT_DEVICE_PAD;
 }
 if(!topspin_controls_guest_sources9(sources))return -1;
 if(!rotate_reader_verified)sources[6]=INPUT_STICK_RIGHT; /* unused/unproved Rotate remains original */
 int result=topspin_prompt_labels9(&identity,sources,labels);
 if(result==1&&!rotate_reader_verified)labels[6]=0;
 return result;
}
static void default_error(void*u,const char*message){(void)u;fprintf(stderr,"[MENU-PROMPTS] ERROR: %s\n",message);}
static int error(const char*message){state.failed=1;state.platform.error(state.platform.user,message);return 0;}
static int configuration_error(const char*message){int result=error(message);state.configured=1;return result;}
static int filename(const char*n){
 if(!n||!*n||strlen(n)>80||strstr(n,".."))return 0;
 for(;*n;n++)if(!((*n>='a'&&*n<='z')||(*n>='A'&&*n<='Z')||(*n>='0'&&*n<='9')||*n=='_'||*n=='.'||*n=='-'))return 0;
 return 1;
}
static uint8_t*readfile(const char*dir,const char*name,size_t*bytes,size_t max){
 if(!filename(name))return NULL;
 char path[1024];
 if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path)return NULL;
 FILE*f=fopen(path,"rb");if(!f)return NULL;
 int good=!fseek(f,0,SEEK_END);long length=good?ftell(f):-1;
 if(length<=0||(unsigned long)length>max||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 uint8_t*p=malloc((size_t)length);if(!p){fclose(f);return NULL;}
 if(fread(p,1,(size_t)length,f)!=(size_t)length){free(p);p=NULL;}
 fclose(f);if(p)*bytes=(size_t)length;return p;
}
int topspin_menu_prompts_configure_image(const char*image,const char*directory,const TopSpinMenuPromptPlatform*platform){
 static const struct {const char*name;uint32_t vptr;} profiles[]={
 {"menu",0x1fc6fc},{"game",0x23aaac},{"training",0x25621c},{"career",0x1db130},
 {"playercreator",0x1fc100},{"shop",0x1cff88},{"online",0x35db50}};
 uint32_t selected=0;
 if(image)for(unsigned i=0;i<sizeof profiles/sizeof profiles[0];i++)if(!strcmp(image,profiles[i].name))selected=profiles[i].vptr;
 if(!selected)return 0;

 int expected=state.configured;
 if(expected!=0&&expected!=-1)return 0;
 if(!atomic_compare_exchange_strong(&state.configured,&expected,-2))return 0;
 InitializeCriticalSection(&runtime_lock);
 resource_vptr=selected;
 rotate_reader_verified=!strcmp(image,"playercreator")||!strcmp(image,"shop");
 guest_budget=(!strcmp(image,"game")||!strcmp(image,"training")||!strcmp(image,"online"))?8u*1024u*1024u:TOPSPIN_MENU_PROMPT_BUDGET;
 const char*trace=getenv("TOPSPIN_VIDEO_TRACE");success_trace=trace&&!strcmp(trace,"1");
 state.platform=platform?*platform:(TopSpinMenuPromptPlatform){NULL,default_map,default_alloc,default_labels,default_error,default_labels9};
 if(!state.platform.map||!state.platform.allocate||(!state.platform.labels&&!state.platform.labels9)||!state.platform.error){state.failed=1;state.configured=1;return 0;}
 if(!directory||strlen(directory)>800)return configuration_error("invalid private resource-cache directory");
 char path[1024];snprintf(path,sizeof path,"%s/manifest.txt",directory);FILE*f=fopen(path,"r");
 if(!f)return configuration_error("private original-resource manifest unavailable; run prepare_menu_prompts.py");
 char line[256];int ok=1;
 while(fgets(line,sizeof line,f)){
  char kind=0,a[81],b[81],extra;Template t={0};unsigned sprite;
  if(state.sources>=MAX_TEMPLATES||!strchr(line,'\n')){ok=0;break;}
  if(sscanf(line,"%c %80s %80s %c",&kind,a,b,&extra)!=3){ok=0;break;}
  if(kind=='F'){
   t.abc=readfile(directory,a,&t.abc_bytes,128*1024);t.xpr=readfile(directory,b,&t.xpr_bytes,2*1024*1024);t.sprite=-1;
   TopSpinRuiLayout layout;if(!t.abc||!t.xpr||!topspin_rui_font_layout(t.abc,t.abc_bytes,&layout)||layout.auxiliary_mask!=3)ok=0;
  }else if(kind=='H'&&!strcmp(a,"0")&&(!strcmp(image,"game")||!strcmp(image,"training"))){
   t.sprite=-2;t.xpr=readfile(directory,b,&t.xpr_bytes,64*1024);if(!t.xpr||!topspin_rui_hud_layout(t.xpr,t.xpr_bytes))ok=0;
  }else if(kind=='S'&&sscanf(a,"%u%c",&sprite,&extra)==1&&sprite<4){t.sprite=(int)sprite;t.xpr=readfile(directory,b,&t.xpr_bytes,64*1024);if(!t.xpr)ok=0;}
  else ok=0;
  if(!ok){free(t.abc);free(t.xpr);break;}
  state.source[state.sources++]=t;
 }
 if(ferror(f))ok=0;
 fclose(f);
 if(!ok||!state.sources)return configuration_error("invalid or unsupported private prompt-resource manifest");
 state.configured=1;return 1;
}
int topspin_menu_prompts_configure(const char*directory,const TopSpinMenuPromptPlatform*platform){
 return topspin_menu_prompts_configure_image("menu",directory,platform);
}
static int enabled(void){
 if(!state.configured){const char*dir=getenv("TOPSPIN_MENU_PROMPTS_DIR");if(!dir||!*dir){int unseen=0;atomic_compare_exchange_strong(&state.configured,&unseen,-1);return 0;}const char*image=getenv("TOPSPIN_PROMPTS_IMAGE");topspin_menu_prompts_configure_image(image?image:"menu",dir,NULL);}
 return state.configured>0&&!state.failed&&state.sources;
}
int topspin_menu_prompts_error(void){
 if(state.configured<=0)return 0;
 EnterCriticalSection(&runtime_lock);int failed=state.failed||state.unsupported;LeaveCriticalSection(&runtime_lock);return failed;
}
size_t topspin_menu_prompts_budget(void){return guest_budget;}
size_t topspin_menu_prompts_pinned(void){
 if(state.configured<=0)return 0;
 EnterCriticalSection(&runtime_lock);size_t bytes=state.pinned;LeaveCriticalSection(&runtime_lock);return bytes;
}
static void retire_locked(uint32_t object){
 for(unsigned i=0;i<MAX_RESOURCES;i++)if(state.resource[i].object==object)memset(&state.resource[i],0,sizeof state.resource[i]);
 /* Template/family variants are shared and pinned, even when an RRTexture
  * object is destroyed. Never free a cached or in-flight header. */
}
static void loaded_locked(uint32_t object){
 retire_locked(object);
 uint8_t*r=state.platform.map(state.platform.user,object,0x32);
 if(!r||word(r)!=resource_vptr||!r[0x30]||r[0x31]||word(r+12)!=object+16)return;
 const uint8_t*header=r+16;uint32_t physical=word(header+4)&0x0fffffffu;
 for(unsigned i=0;i<state.sources;i++){
  const Template*t=&state.source[i];if(t->xpr_bytes<32||(word(header)&0x70000)!=0x40000||word(header+12)!=word(t->xpr+24)||word(header+16)!=word(t->xpr+28))continue;
  size_t n=t->xpr_bytes-32;const void*data=state.platform.map(state.platform.user,physical|0x80000000u,n);
  if(!data||memcmp(data,t->xpr+32,n))continue;
  for(unsigned j=0;j<MAX_RESOURCES;j++)if(!state.resource[j].object){state.resource[j]=(Resource){object,object+16,i};
   if(success_trace&&association_logs<8){association_logs++;fprintf(stderr,"[MENU-PROMPTS] associated resource=%08X header=%08X source=%u\n",object,object+16,i);}
   return;}
  error("live prompt-resource identity limit exhausted");return;
 }
}
static int select_source_locked(unsigned source,uint32_t original,uint32_t*selected){
 *selected=original;
 if(state.failed)return 0;
 uint8_t labels[9]={0};int label_status=state.platform.labels9?state.platform.labels9(state.platform.user,labels):state.platform.labels(state.platform.user,labels);
 if(!label_status)return 1; /* no physical activity yet: preserve original until known */
 if(label_status<0){
  if(!state.unsupported)state.platform.error(state.platform.user,"active input has no supported physical prompt projection (keyboard/non-face bindings remain pending)");
  state.unsupported=1;return 0;
 }
 state.unsupported=0;
 /* Canonical key excludes glyphs absent in this exact resource. */
 Template*t=&state.source[source];unsigned glyph_mask;
 if(t->sprite==-2)glyph_mask=48u;
 else if(t->sprite>=0)glyph_mask=1u<<t->sprite;
 else{TopSpinRuiLayout layout;if(!topspin_rui_font_layout(t->abc,t->abc_bytes,&layout))return error("invalid cached ABC");glyph_mask=63u|(layout.extra_mask<<6);}
 uint8_t key[9];for(unsigned k=0;k<9;k++)key[k]=(glyph_mask&(1u<<k))?labels[k]:0;
 for(unsigned i=0;i<state.variants;i++)if(state.variant[i].source==source&&!memcmp(state.variant[i].labels,key,9)){*selected=state.variant[i].header;return 1;}
 if(state.variants>=MAX_VARIANTS)return error("immutable prompt-variant count exhausted; no in-flight allocation was freed");
 uint8_t*owned=NULL;size_t bytes=0;
 int ok=t->sprite==-2?topspin_rui_hud_project(t->xpr,t->xpr_bytes,labels,&owned,&bytes):t->sprite<0?topspin_rui_font_project_controls9(t->abc,t->abc_bytes,t->xpr,t->xpr_bytes,labels,&owned,&bytes):topspin_rui_sprite_project9(t->xpr,t->xpr_bytes,labels[t->sprite],&owned,&bytes);
 if(!ok||bytes<32){free(owned);return error("owned glyph projection rejected original resource");}
 size_t allocation=(128+bytes-32+4095u)&~(size_t)4095u;
 if(allocation>guest_budget-state.pinned){free(owned);return error("profile guest prompt pin budget exhausted; no fallback was silently accepted");}
 uint32_t address=state.platform.allocate(state.platform.user,(uint32_t)allocation,4096);
 if(!address){free(owned);return error("guest allocation for immutable prompt texture failed");}
 state.pinned+=allocation;uint8_t*dst=state.platform.map(state.platform.user,address,allocation);
 if(!dst||address<0x80000000u||(uint64_t)address+allocation>0x84000000u){free(owned);return error("guest allocator returned an invalid physical texture range; allocation kept pinned");}
 memset(dst,0,128);memcpy(dst,owned+12,20);store(dst+4,(address+128)&0x0fffffffu);memcpy(dst+128,owned+32,bytes-32);free(owned);
 Variant*v=&state.variant[state.variants++];v->source=source;memcpy(v->labels,key,9);v->header=address;*selected=address;
 if(success_trace&&upload_logs<32){upload_logs++;fprintf(stderr,"[MENU-PROMPTS] uploaded source=%u labels=%u,%u,%u,%u,%u,%u bytes=%zu pinned=%zu\n",source,labels[0],labels[1],labels[2],labels[3],labels[4],labels[5],allocation,state.pinned);}
 return 1;
}
static int select_locked(uint32_t original,uint32_t*selected){
 *selected=original;if(state.failed)return 0;
 for(unsigned i=0;i<MAX_RESOURCES;i++)if(state.resource[i].object&&state.resource[i].header==original)return select_source_locked(state.resource[i].source,original,selected);
 return 1;
}
/* Native HUD has no RRTexture wrapper. Authenticate Format/Size and every
 * original mip byte at each bind; pointer/address reuse cannot inherit a match. */
static int select_hud_for_caller(uint32_t original,uint32_t*selected,uint32_t caller){
 if(!selected)return 0;*selected=original;if(!enabled())return !state.failed;
 EnterCriticalSection(&runtime_lock);int ok=1;
 const uint8_t*h=state.platform.map(state.platform.user,original,20);
 if(h&&(word(h)&0x70000u)==0x40000u)for(unsigned i=0;i<state.sources;i++){
  Template*t=&state.source[i];if(t->sprite!=-2||word(h+12)!=word(t->xpr+24)||word(h+16)!=word(t->xpr+28))continue;
  size_t n=t->xpr_bytes-32;const void*p=state.platform.map(state.platform.user,(word(h+4)&0x0fffffffu)|0x80000000u,n);
  if(p&&!memcmp(p,t->xpr+32,n)){ok=select_source_locked(i,original,selected);
   if(success_trace&&hud_logs<24){hud_logs++;fprintf(stderr,"[HUD-PROMPTS] caller=%08X source=%u header=%08X selected=%08X ok=%d\n",caller,i,original,*selected,ok);}break;}
 }
 LeaveCriticalSection(&runtime_lock);return ok;
}
int topspin_menu_prompts_select_hud(uint32_t original,uint32_t*selected){return select_hud_for_caller(original,selected,0);}
uint32_t topspin_prompt_hud_begin(uint32_t object,uint32_t caller){
 if(!enabled()||object>UINT32_MAX-0x40u)return 0;
 uint8_t*p=state.platform.map(state.platform.user,object+0x40u,4);if(!p)return 0;
 uint32_t original=word(p),selected=original;
 if(select_hud_for_caller(original,&selected,caller)&&selected!=original){store(p,selected);return original;}
 return 0;
}
void topspin_prompt_hud_end(uint32_t object,uint32_t original){
 if(!original)return;uint8_t*p=state.platform.map(state.platform.user,object+0x40u,4);if(p)store(p,original);
}
/* Every entry uses the runtime compatibility lock, whose contended acquire
 * releases guest execution ownership while waiting. Plain pthread locks
 * across HostInput/allocator callbacks could deadlock another guest thread. */
void topspin_menu_prompts_retire(uint32_t object){
 if(!enabled())return;
 EnterCriticalSection(&runtime_lock);retire_locked(object);LeaveCriticalSection(&runtime_lock);
}
void topspin_menu_prompts_loaded(uint32_t object){
 if(!enabled())return;
 EnterCriticalSection(&runtime_lock);loaded_locked(object);LeaveCriticalSection(&runtime_lock);
}
int topspin_menu_prompts_select(uint32_t original,uint32_t*selected){
 if(!selected)return 0;
 *selected=original;if(!enabled())return !state.failed;
 EnterCriticalSection(&runtime_lock);int ok=select_locked(original,selected);LeaveCriticalSection(&runtime_lock);return ok;
}
void topspin_prompt_loaded_hook(void(*original)(void)){
 uint32_t object=g_ecx;original();if(g_eax&255u){topspin_menu_prompts_loaded(object);sxui_loaded(object);}
}
void topspin_prompt_retire_hook(void(*original)(void)){
 sxui_retire(g_ecx);topspin_menu_prompts_retire(g_ecx);original();
}
void topspin_prompt_bind_hook(void(*callee)(void)){
 uint8_t*arg=enabled()?state.platform.map(state.platform.user,g_esp+8,4):NULL;
 uint32_t original=arg?word(arg):0,selected=original;
 if(arg&&topspin_menu_prompts_select(original,&selected)&&selected!=original)store(arg,selected);
 callee();
 if(arg&&selected!=original)store(arg,original);
}
