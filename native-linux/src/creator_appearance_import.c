/* Opt-in private original-API appearance experiment. Own work: djultra64.
 * No Career-valid flag/signature synthesis. The real original model parser and
 * writer are called; only an exact ADN roundtrip is reported as such. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "creator_appearance_import.h"
#include "creator_ui_diagnostic.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "menu_pc_sha256.h"
#include "appearance_equivalence.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <fenv.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
extern _Thread_local uint32_t g_itail_site;
extern void sub_0005B270_gen(void);
extern void sub_0005B230_gen(void);
extern void sub_00060220(void),sub_000C4A70(void),sub_000C63F0(void),sub_000C43F0(void),sub_000C4310(void),sub_00062D00(void),sub_000637D0(void);
extern void sub_00118F00(void);
extern void sub_000BC840(void),sub_000BD9F0(void),sub_000BB680(void);
extern void sub_000DA380(void),sub_000DA470(void),sub_000DA5A0(void),sub_000DA0E0(void),sub_000CDEC0(void);
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

static struct {int enabled,attempted,diagnostic,canonical,case_capture,case_attempted;unsigned rejected,observed;uint32_t case_prng_before[4];char root[PATH_MAX];unsigned char expected[1973];} imp;
static _Thread_local int internal;
static const char slot[]="Player\\";
static int file_read(const char*path,void*dst,size_t exact){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return 0;
 struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size==(off_t)exact;
 size_t at=0;while(ok&&at<exact){ssize_t n=read(fd,(unsigned char*)dst+at,exact-at);if(n<=0){ok=0;break;}at+=(size_t)n;}
 struct stat after;
 if(ok&&(fstat(fd,&after)||st.st_dev!=after.st_dev||st.st_ino!=after.st_ino||st.st_size!=after.st_size||st.st_mtim.tv_sec!=after.st_mtim.tv_sec||st.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||st.st_ctim.tv_sec!=after.st_ctim.tv_sec||st.st_ctim.tv_nsec!=after.st_ctim.tv_nsec))ok=0;
 if(close(fd))ok=0;
 return ok;
}
static int path_join(char*out,size_t n,const char*a,const char*b){int k=snprintf(out,n,"%s/%s",a,b);return k>=0&&(size_t)k<n;}
static int private_cache_dir(const char*root,char*out,size_t size){
 char actual[PATH_MAX];struct stat st;
 return path_join(out,size,root,"Cache")&&!lstat(out,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&realpath(out,actual)&&!strcmp(out,actual);
}
int topspin_creator_appearance_configure(const char*saves_root){
 const char*capture=getenv("TOPSPIN_CREATOR_CASE_CAPTURE");
 if(capture&&!strcmp(capture,"1")){
  const char*import=getenv("TOPSPIN_PRIVATE_APPEARANCE_IMPORT");
  char root[PATH_MAX],cache[PATH_MAX],parent[PATH_MAX],path[PATH_MAX];struct stat st;
  if(imp.case_capture||imp.enabled||(import&&!strcmp(import,"1"))||!saves_root||!realpath(saves_root,root)||!strstr(root,"/reports/")||strlen(root)>=sizeof parent)return 0;
  strcpy(parent,root);char*slash=strrchr(parent,'/');if(!slash||strcmp(slash,"/saves"))return 0;*slash=0;
  if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0777)!=0700||!private_cache_dir(root,cache,sizeof cache))return 0;
  const char*outputs[]={"CreatorCase.ADN","CreatorCase.decoded.adn","CreatorCase.json"};
  for(unsigned i=0;i<3;i++)if(!path_join(path,sizeof path,cache,outputs[i])||!lstat(path,&st)||errno!=ENOENT)return 0;
  strcpy(imp.root,root);imp.case_capture=1;return 1;
 }
 const char*e=getenv("TOPSPIN_PRIVATE_APPEARANCE_IMPORT");if(!e||strcmp(e,"1"))return 1;
 if(imp.enabled||!saves_root)return 0;
 char root[PATH_MAX],path[PATH_MAX],marker[PATH_MAX+64],expected_marker[PATH_MAX+64];
 if(!realpath(saves_root,root)||!strstr(root,"/reports/")||strlen(root)+80>=sizeof path)return 0;
 const char*slash=strrchr(root,'/');if(!slash||strcmp(slash,"/saves"))return 0;
 char parent[PATH_MAX];memcpy(parent,root,(size_t)(slash-root));parent[slash-root]=0;
 struct stat parent_stat;
 if(stat(parent,&parent_stat)||!S_ISDIR(parent_stat.st_mode)||parent_stat.st_uid!=getuid()||(parent_stat.st_mode&0777)!=0700)return 0;
 if(!private_cache_dir(root,path,sizeof path))return 0;
 const char*dirs[]={"UserData/4d530035/12E9194916CD"};char resolved[PATH_MAX];
 for(unsigned i=0;i<1;i++)if(!path_join(path,sizeof path,root,dirs[i])||!realpath(path,resolved)||strcmp(path,resolved))return 0;
 int n=snprintf(expected_marker,sizeof expected_marker,"APPEARANCE-ONLY-PRIVATE-V1\n%s\n",root);
 if(n<=0||(size_t)n>=sizeof marker||!path_join(path,sizeof path,parent,"appearance-only.token")||!file_read(path,marker,(size_t)n)||memcmp(marker,expected_marker,(size_t)n))return 0;
 unsigned char ini[198],adn[808];
 if(!path_join(path,sizeof path,root,"UserData/4d530035/12E9194916CD/Player.ini")||!file_read(path,ini,sizeof ini)||!menu_pc_sha_is(ini,sizeof ini,"ffa68a958f9925169d7d3a03c1f0e3fe44991cc651e9993870e9a37c62f031ea"))return 0;
 if(!path_join(path,sizeof path,root,"UserData/4d530035/12E9194916CD/PlayerCreator00.ADN")||!file_read(path,adn,sizeof adn)||!menu_pc_sha_is(adn,sizeof adn,"f7e7999f68ed9844e4f8f459bfa295e10021b01c853187333a3d8fdc47545069"))return 0;
 if(!path_join(path,sizeof path,parent,"appearance-original.decoded.adn")||!file_read(path,imp.expected,sizeof imp.expected)||!menu_pc_sha_is(imp.expected,sizeof imp.expected,"5bc0925a6a8754e77b1fc2f42ce82ab48c2536ed217a4dba792a7039b7acbf3a"))return 0;
 struct stat output_stat;const char*diag=getenv("TOPSPIN_PRIVATE_APPEARANCE_DIAGNOSTIC");int diagnostic=diag&&!strcmp(diag,"1");
 const char*outputs[]={"Cache/AppearanceImportRoundtrip.ADN","Cache/AppearanceImportBefore.ADN","Cache/AppearanceImportAfter.ADN","Cache/AppearanceImportSecond.ADN"};
 for(unsigned i=diagnostic?1:0;i<(diagnostic?4:1);i++)if(!path_join(path,sizeof path,root,outputs[i])||!lstat(path,&output_stat)||errno!=ENOENT)return 0; /* never overwrite an earlier experiment */
 const char*canon_env=getenv("TOPSPIN_PRIVATE_APPEARANCE_CANONICAL_IMPORT");int canonical=canon_env&&!strcmp(canon_env,"1");
 if(canonical&&diagnostic)return 0;
 if(canonical){
  const char*canonical_outputs[]={"Cache/AppearanceCanonicalAfter.ADN","Cache/AppearanceCanonicalSecond.ADN"};
  for(unsigned i=0;i<2;i++)if(!path_join(path,sizeof path,root,canonical_outputs[i])||!lstat(path,&output_stat)||errno!=ENOENT)return 0;
 }
 imp.diagnostic=diagnostic;imp.canonical=canonical;
 strcpy(imp.root,root);imp.enabled=1;fputs("[APPEARANCE-IMPORT] configure enabled=1 private marker/source/reference accepted; import requires actual hub guards\n",stderr);return 1;
}
static void*map(uint32_t p,size_t n){return p>=0x10000u&&(uint64_t)p+n<=0x4000000u?(void*)((uintptr_t)g_xbox_mem_offset+p):NULL;}
static uint32_t word(uint32_t p){uint32_t x=0;void*q=map(p,4);if(q)memcpy(&x,q,4);return x;}
static int string_is(uint32_t p,const char*s){size_t n=strlen(s)+1;void*q=map(p,n);return q&&!memcmp(q,s,n);}
static int scene_is_hub(uint32_t scene){uint32_t p=word(scene+0x18c);return map(scene,0x209)&&p>=0x10010u&&word(p-12)==6&&string_is(p,"pc_hub");}
static uint32_t call_sp;
static int call(void(*fn)(void),uint32_t object,unsigned n,const uint32_t*args,uint32_t*out){
 uint32_t*stack=map(call_sp,4+4*n);if(!stack)return 0;stack[0]=0;for(unsigned i=0;i<n;i++)stack[i+1]=args[i];g_esp=call_sp;g_ecx=object;fn();if(out)*out=g_eax;return g_esp==call_sp+4+4*n;
}
static int call_cdecl(void(*fn)(void),unsigned n,const uint32_t*args,uint32_t*out){
 uint32_t*stack=map(call_sp,4+4*n);if(!stack)return 0;stack[0]=0;for(unsigned i=0;i<n;i++)stack[i+1]=args[i];g_esp=call_sp;fn();if(out)*out=g_eax;return g_esp==call_sp+4;
}
static int decoded_roundtrip(uint32_t guest_path,uint32_t buffer){
 uint32_t openargs[]={guest_path,0x8000},stream=0,bytes=0;
 if(!call_cdecl(sub_000BC840,2,openargs,&stream)||!map(stream,0x24)||!map(word(stream),0x24))return 0;
 int exact=0;uint32_t target=word(word(stream)+0x20);
 if(call(sub_000BD9F0,stream,0,NULL,&bytes)&&bytes==sizeof imp.expected&&target>=0x10000u&&target<0x1d0000u){
  recomp_func_t readfn=recomp_lookup(target);uint32_t readargs[]={buffer,sizeof imp.expected};
  uint32_t received=0;
  if(readfn&&word(stream+8)==0&&call(readfn,stream,2,readargs,&received)&&received==sizeof imp.expected&&word(stream+8)==sizeof imp.expected&&!memcmp(map(buffer,sizeof imp.expected),imp.expected,sizeof imp.expected))exact=1;
 }
 uint32_t closeargs[]={stream};if(!call_cdecl(sub_000BB680,1,closeargs,NULL))exact=0;
 return exact;
}
/* Diagnostic decoding uses the original stream, including PAM-ZLB handling.
 * Guest buffer occupies base+2800..37FF, inside the saved 16KiB region. */
static int diagnostic_decode(uint32_t guest_path,uint32_t buffer,unsigned char*out,uint32_t*size){
 uint32_t args[]={guest_path,0x8000},stream=0,bytes=0;int ok=0;
 if(!call_cdecl(sub_000BC840,2,args,&stream)||!map(stream,0x24)||!map(word(stream),0x24))return 0;
 uint32_t target=word(word(stream)+0x20);
 if(call(sub_000BD9F0,stream,0,NULL,&bytes)&&bytes&&bytes<=4096&&target>=0x10000u&&target<0x1d0000u){
  recomp_func_t fn=recomp_lookup(target);uint32_t readargs[]={buffer,bytes};
  uint32_t received=0;
  /* Base stream Read20 returns the actual byte count and advances +8
   * (original BDB90/BDBD9); short reads are not decoded snapshots. */
  if(fn&&word(stream+8)==0&&call(fn,stream,2,readargs,&received)&&received==bytes&&word(stream+8)==bytes){memcpy(out,map(buffer,bytes),bytes);*size=bytes;ok=1;}
 }
 uint32_t closeargs[]={stream};if(!call_cdecl(sub_000BB680,1,closeargs,NULL))ok=0;return ok;
}
static int diagnostic_save(uint32_t model,uint32_t path,uint32_t buffer,const char*name,unsigned char*out,uint32_t*size){
 char relative[128],host[PATH_MAX],cache[PATH_MAX];struct stat st;int n=snprintf(relative,sizeof relative,"Cache/%s",name);
 if(n<0||(size_t)n>=sizeof relative||!private_cache_dir(imp.root,cache,sizeof cache)||!path_join(host,sizeof host,imp.root,relative)||!lstat(host,&st)||errno!=ENOENT)return 0;
 n=snprintf(map(path,128),128,"Z:\\%s",name);if(n<0||n>=128)return 0;
 uint32_t args[]={path};return call(sub_000C43F0,model,1,args,NULL)&&diagnostic_decode(path,buffer,out,size);
}
static int case_write(const char*name,const void*data,size_t bytes){
 char cache[PATH_MAX],path[PATH_MAX];
 if(!private_cache_dir(imp.root,cache,sizeof cache)||!path_join(path,sizeof path,cache,name))return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 size_t at=0;while(at<bytes){ssize_t n=write(fd,(const unsigned char*)data+at,bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);return 0;}at+=(size_t)n;}
 return close(fd)==0;
}
/* This is an original appearance serialization, not Career Save or an import.
 * It runs only once after the authentic Loading -> pc_hub completion. */
static void case_checkpoint(uint32_t scene){
 if(!imp.case_capture||imp.case_attempted||internal||!scene_is_hub(scene))return;
 uint32_t model=word(0x390a54),metadata=word(0x2b7764);
 if(!map(model,0x24)||word(model)!=0x1eddbcu||!map(metadata,0x50)||g_esp<0x15000u||g_esp>0x4000000u)return;
 unsigned gender=*(unsigned char*)map(model+0x18,1);
 if(gender>1||gender!=*(unsigned char*)map(metadata+0x1c,1))return;
 imp.case_attempted=1;
 Cpu cpu;cpu_save(&cpu);unsigned char borrowed[0x4000],decoded[4096];uint32_t bytes=0,base=g_esp-0x4000,previous_sp=call_sp;
 memcpy(borrowed,map(base,sizeof borrowed),sizeof borrowed);call_sp=base+0x1800;internal=1;
 int ok=diagnostic_save(model,base+0x2600,base+0x2800,"CreatorCase.ADN",decoded,&bytes);
 char hash[65]={0};if(ok)menu_pc_sha256(decoded,bytes,hash);
 if(ok)ok=case_write("CreatorCase.decoded.adn",decoded,bytes);
 if(ok){char json[768];int n=snprintf(json,sizeof json,"{\"version\":1,\"snapshot_complete\":true,\"gender\":%u,\"decoded_adn_bytes\":%u,\"decoded_adn_sha256\":\"%s\",\"scene\":\"pc_hub\",\"prng_before\":[%u,%u,%u,%u],\"prng_after\":[%u,%u,%u,%u]}\n",gender,bytes,hash,imp.case_prng_before[0],imp.case_prng_before[1],imp.case_prng_before[2],imp.case_prng_before[3],word(0x2b16ec),word(0x2b16f0),word(0x2b16f4),word(0x2b16f8));ok=n>0&&(size_t)n<sizeof json&&case_write("CreatorCase.json",json,(size_t)n);}
 memcpy(map(base,sizeof borrowed),borrowed,sizeof borrowed);internal=0;call_sp=previous_sp;cpu_restore(&cpu);
 fprintf(stderr,"[CREATOR-CASE] ready=pc_hub gender=%u original_ADN_complete=%d bytes=%u sha256=%s; no import or Career Save\n",gender,ok,bytes,hash);
}
static int text_property(uint32_t rootref,const char*property,uint32_t scratch,uint32_t*out){
 uint32_t r0=scratch,r1=scratch+16,a=scratch+32,key=scratch+64;
 if(!call(sub_000DA380,r0,0,NULL,NULL)||!call(sub_000DA380,r1,0,NULL,NULL))return 0;
 strcpy(map(key,32),"TRPlayer");uint32_t args[]={r0,key};int ok=call(sub_000DA470,rootref,2,args,NULL);
 strcpy(map(key,32),"TRPlayerContent");args[0]=r1;ok=ok&&call(sub_000DA470,r0,2,args,NULL);
 strcpy(map(key,32),property);args[0]=a;ok=ok&&call(sub_000DA5A0,r1,2,args,NULL);
 uint32_t fallback[]={key+32};*(char*)map(key+32,1)=0;ok=ok&&call(sub_000DA0E0,a,1,fallback,out);
 int released=call(sub_000CDEC0,r1,0,NULL,NULL)&&call(sub_000CDEC0,r0,0,NULL,NULL);return ok&&released;
}
static void safe_text(uint32_t p,char*out,size_t capacity){
 if(!capacity)return;
 out[0]=0;
 for(size_t i=0;i+1<capacity;i++){
  const unsigned char*c=map(p+(uint32_t)i,1);if(!c)return;
  if(!*c){out[i]=0;return;}
  out[i]=*c>=32&&*c<127?(char)*c:'?';out[i+1]=0;
 }
}
static void trace_initialize(uint32_t scene,uint32_t entry_ecx,uint32_t entry_stack){
 if(!imp.enabled||internal||imp.observed>=6)return;
 imp.observed++;
 uint32_t manager=word(0x390a5c),model=word(0x390a54),metadata=word(0x2b7764),context=word(0x2b7768);
 unsigned guards=(map(manager,0x3c0)?1u:0u)|(map(model,0x24)?2u:0u)|(word(model)==0x1eddbcu?4u:0u)|(map(metadata,0x50)?8u:0u)|(map(context,0x24)?16u:0u)|(word(context+8)==manager?32u:0u);
 unsigned gender_meta=map(metadata+0x1c,1)?*(unsigned char*)map(metadata+0x1c,1):255,gender_model=map(model+0x18,1)?*(unsigned char*)map(model+0x18,1):255;
 char scene_name[64],metadata_name[64]="<not-read>";uint32_t nameptr=word(scene+0x18c);safe_text(nameptr,scene_name,sizeof scene_name);
 uint32_t document=word(metadata+0x10),backend=word(document),backend_vtable=word(backend);
 if(map(metadata,0x50)&&map(document,0x68)&&map(backend,0x14)&&(backend_vtable==0x1f2f68u||backend_vtable==0x1f3018u)&&g_esp>=0x15000u&&g_esp<=0x4000000u){
  Cpu cpu;cpu_save(&cpu);unsigned char borrowed[0x4000];uint32_t base=g_esp-0x4000;memcpy(borrowed,map(base,sizeof borrowed),sizeof borrowed);uint32_t old_sp=call_sp;call_sp=base+0x1800;internal=1;uint32_t text=0;
  if(text_property(metadata+0x14,"Name",base+0x2000,&text))safe_text(text,metadata_name,sizeof metadata_name);
  memcpy(map(base,sizeof borrowed),borrowed,sizeof borrowed);internal=0;call_sp=old_sp;cpu_restore(&cpu);
 }
 fprintf(stderr,"[APPEARANCE-IMPORT] observe=%u candidate=%08X entry_ecx=%08X caller=%08X arg0=%08X arg1=%08X scene_vtable=%08X scene_name_ptr=%08X scene_name_len=%u scene_name=\"%s\" hub=%d manager=%08X model=%08X model_vtable=%08X metadata=%08X context=%08X context_owner=%08X guards=%02X genders=%u/%u document=%08X backend_vtable=%08X metadata_name=\"%s\"\n",imp.observed,scene,entry_ecx,word(entry_stack),word(entry_stack+4),word(entry_stack+8),word(scene),nameptr,nameptr>=12?word(nameptr-12):0,scene_name,scene_is_hub(scene),manager,model,word(model),metadata,context,word(context+8),guards,gender_meta,gender_model,document,backend_vtable,metadata_name);
}
static void post_initialize(uint32_t scene){
 if(!imp.enabled||internal||imp.attempted||!scene_is_hub(scene))return;
 uint32_t manager=word(0x390a5c),model=word(0x390a54),metadata=word(0x2b7764),context=word(0x2b7768);
 if(!map(manager,0x3c0)||!map(model,0x24)||word(model)!=0x1eddbcu||!map(metadata,0x50)||!map(context,0x24)||word(context+8)!=manager||*(unsigned char*)map(metadata+0x1c,1)!=1||*(unsigned char*)map(model+0x18,1)!=1)return;
 if(g_esp<0x15000||g_esp>0x4000000)return;
 Cpu cpu;cpu_save(&cpu);unsigned char borrowed[0x4000];uint32_t base=g_esp-0x4000;memcpy(borrowed,map(base,sizeof borrowed),sizeof borrowed);call_sp=base+0x1800;internal=1;
 uint32_t scratch=base+0x2000,descriptor=base+0x2400,path=base+0x2600,text=0;
 int ok=text_property(metadata+0x14,"Name",scratch,&text)&&string_is(text,"Player");
 if(ok){ok=text_property(metadata+0x14,"CodeName",scratch,&text)&&(string_is(text,"")||string_is(text,slot));}
 if(!ok){if(imp.rejected++<4)fputs("[APPEARANCE-IMPORT] hub rejected: Name/CodeName metadata mismatch; no model import\n",stderr);goto done;}
 strcpy(map(path,sizeof slot),slot);uint32_t lookup_args[]={path,scratch+128};*(uint32_t*)map(scratch+128,4)=0;
 ok=call_cdecl(sub_00118F00,2,lookup_args,NULL)&&string_is(word(scratch+128),"U:\\12E9194916CD\\");
 if(!ok){if(imp.rejected++<4)fputs("[APPEARANCE-IMPORT] original118F00 logical Player does not resolve authenticated private slot; no model import\n",stderr);goto done;}
 imp.attempted=1;
 ok=call(sub_00060220,descriptor,0,NULL,NULL);strcpy(map(path,sizeof slot),slot);uint32_t args[]={path};ok=ok&&call(sub_000C4A70,descriptor,1,args,NULL);
 unsigned char before[4096],after[4096],second[4096];uint32_t before_n=0,after_n=0,second_n=0;
 if(imp.diagnostic){
  int saved=diagnostic_save(model,path,base+0x2800,"AppearanceImportBefore.ADN",before,&before_n);
  fprintf(stderr,"[APPEARANCE-IMPORT] diagnostic Before original_C43F0_BC840=%d decoded_bytes=%u; no metadata export\n",saved,before_n);
  if(!saved)goto done;
 }
 /* Exact FC9BB original CodeName-consumer setup: disable loading-render mode,
  * not a Career flag. Constructor's ADN index00 and +120 remain untouched. */
 *(unsigned char*)map(descriptor+0x109,1)=0;args[0]=descriptor;uint32_t result=0;
 ok=ok&&call(sub_000C63F0,manager,1,args,&result)&&(result&255u)==1;
 fprintf(stderr,"[APPEARANCE-IMPORT] original C63F0 slot=%s index=00 result=%u; Career validation untouched\n",slot,result&255u);
 if(!ok)goto done;
 model=word(0x390a54);if(!map(model,0x24)||word(model)!=0x1eddbcu||*(unsigned char*)map(model+0x18,1)!=1)goto done;
 if(imp.diagnostic){
  int saved=diagnostic_save(model,path,base+0x2800,"AppearanceImportAfter.ADN",after,&after_n);
  fprintf(stderr,"[APPEARANCE-IMPORT] diagnostic After original_C43F0_BC840=%d decoded_bytes=%u old_exact=%d; no metadata export\n",saved,after_n,saved&&after_n==sizeof imp.expected&&!memcmp(after,imp.expected,after_n));
  if(!saved)goto done;
  /* C4310(this=model,path,append,filter) ret12. Z branch concatenates the
   * two strings, opens BC840, and calls the original C3D00 model parser. */
  uint32_t empty=base+0x2300;*(unsigned char*)map(empty,1)=0;uint32_t reloadargs[]={path,empty,0};uint32_t loaded=0;
  int abi=call(sub_000C4310,model,3,reloadargs,&loaded);
  fprintf(stderr,"[APPEARANCE-IMPORT] diagnostic original_C4310 ZAfter reload_abi=%d reload_AL=%u; no metadata export\n",abi,loaded&255u);
  if(!abi||(loaded&255u)!=1)goto done;
  saved=diagnostic_save(model,path,base+0x2800,"AppearanceImportSecond.ADN",second,&second_n);
  fprintf(stderr,"[APPEARANCE-IMPORT] diagnostic Second original_C43F0_BC840=%d decoded_bytes=%u canonical_equal=%d before_equal=%d; NEVER metadata export or Career Save\n",saved,second_n,saved&&after_n==second_n&&!memcmp(after,second,after_n),saved&&before_n==second_n&&!memcmp(before,second,before_n));
  goto done;
 }
 if(imp.canonical){
  int saved=diagnostic_save(model,path,base+0x2800,"AppearanceCanonicalAfter.ADN",after,&after_n);
  int preserved=saved&&topspin_creator_appearance_equivalent(imp.expected,sizeof imp.expected,after,after_n,after,after_n);
  fprintf(stderr,"[APPEARANCE-IMPORT] canonical After original_C43F0_BC840=%d decoded_bytes=%u source_preserved=%d; no Career progress recovery\n",saved,after_n,preserved);
  if(!preserved)goto done;
  uint32_t empty=base+0x2300;*(unsigned char*)map(empty,1)=0;uint32_t reloadargs[]={path,empty,0},loaded=0;
  int abi=call(sub_000C4310,model,3,reloadargs,&loaded);
  fprintf(stderr,"[APPEARANCE-IMPORT] canonical original_C4310 reload_abi=%d reload_AL=%u\n",abi,loaded&255u);
  if(!abi||(loaded&255u)!=1)goto done;
  saved=diagnostic_save(model,path,base+0x2800,"AppearanceCanonicalSecond.ADN",second,&second_n);
  int equivalent=saved&&topspin_creator_appearance_equivalent(imp.expected,sizeof imp.expected,after,after_n,second,second_n);
  fprintf(stderr,"[APPEARANCE-IMPORT] canonical Second original_C43F0_BC840=%d decoded_bytes=%u source_preserved_and_reload_exact=%d; appearance-only, not Career validity\n",saved,second_n,equivalent);
  if(!equivalent)goto done;
 }else{
 static const char output[]="Z:\\AppearanceImportRoundtrip.ADN";strcpy(map(path,sizeof output),output);args[0]=path;
 char filename[PATH_MAX],cache_dir[PATH_MAX];struct stat previous;
 if(!private_cache_dir(imp.root,cache_dir,sizeof cache_dir)||!path_join(filename,sizeof filename,imp.root,"Cache/AppearanceImportRoundtrip.ADN")||!lstat(filename,&previous)||errno!=ENOENT)goto done;
 if(!call(sub_000C43F0,model,1,args,NULL))goto done;
 int exact=decoded_roundtrip(path,base+0x2800);
 fprintf(stderr,"[APPEARANCE-IMPORT] original C43F0+BC840 decoded_ADN roundtrip_exact=%d bytes=1973; appearance-only, no progress recovery\n",exact);
 if(!exact)goto done;
 }
 strcpy(map(path,sizeof slot),slot);args[0]=path;
 if(!call(sub_00062D00,metadata,1,args,NULL)||!call(sub_000637D0,metadata,0,NULL,NULL))goto done;
 fputs("[APPEARANCE-IMPORT] original CodeName setter62D00 and cache export637D0 completed; subsequent hub/save still requires private validation\n",stderr);
done:
 memcpy(map(base,sizeof borrowed),borrowed,sizeof borrowed);internal=0;cpu_restore(&cpu);
}
void sub_0005B270(void){uint32_t scene=0,entry_ecx=0,entry_stack=0;if((imp.enabled||imp.case_capture)&&!internal){entry_ecx=g_ecx;entry_stack=g_esp;scene=word(g_esp+4);if(imp.case_capture&&!imp.case_attempted)for(unsigned i=0;i<4;i++)imp.case_prng_before[i]=word(0x2b16ec+4*i);}sub_0005B270_gen();trace_initialize(scene,entry_ecx,entry_stack);}

/* Loading's original per-frame callback performs the completed transition;
 * import only after that original call has activated the cached hub. */
void sub_0005B230(void){
 uint32_t loading=0,context=0,manager=0,hub=0;int completed=0;
 const char*ui=getenv("TOPSPIN_CREATOR_UI_DIAGNOSTICS");int ui_trace=ui&&!strcmp(ui,"1");
 if((imp.enabled||imp.case_capture||ui_trace)&&!internal&&!imp.attempted){
  loading=g_ecx;context=word(0x2b7768);manager=word(0x390a5c);
  uint32_t name=word(loading+0x18c);
  completed=map(loading,0x209)&&name>=0x10010u&&word(name-12)==23&&string_is(name,"pc_LoadingPlayerCreator")&&*(unsigned char*)map(loading+0x19c,1)==1&&*(unsigned char*)map(loading+0x208,1)==1&&map(context,0x24)&&word(context+8)==manager;
  if(completed)hub=word(context+0xc);
 }
 sub_0005B230_gen();
 if(ui_trace)topspin_creator_ui_observe(loading,context,manager,hub);
 if(!completed||!(g_eax&255u)||word(0x2b7768)!=context||word(0x390a5c)!=manager||word(context+8)!=manager||word(context+0xc)!=hub||!scene_is_hub(hub)||*(unsigned char*)map(hub+0x19c,1)!=1||*(unsigned char*)map(loading+0x208,1)!=0||*(unsigned char*)map(loading+0x19c,1)!=0)return;
 if(imp.case_capture)case_checkpoint(hub);
 if(imp.enabled){fputs("[APPEARANCE-IMPORT] original5B230 completed Loading-to-hub transition; validating private appearance import\n",stderr);post_initialize(hub);}
}
