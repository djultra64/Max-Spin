/* Original C43F0/C4310 restore, no Creator reinitialization. djultra64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define RECOMP_GENERATED_CODE
#include "playercreator_model_checkpoint.h"
#include "material_save_operation.h"
#include "actor_binding_cpu_guard.h"
#include "recomp_funcs.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define ADN_MAX 4096u
#define GRAPH_MAX 512u
static int configured,enabled,observe_only;
static char cache_root[PATH_MAX];
typedef struct {uint32_t va,vt,type,active,raw_5c,active_node,descriptor,index,table,render,render_vt,render_callback4,render_valid,render_raw[8];} Node;
static _Thread_local struct {
 int active,busy,retain;unsigned kind;uint32_t owner,model,gender,size;
 unsigned char adn[ADN_MAX];char directory[PATH_MAX],guest[128];
 unsigned char*encoded;size_t encoded_bytes;
 Node before[GRAPH_MAX];unsigned nodes,observations;
} cp;
static void*ram(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u?(void*)XBOX_PTR(a):NULL;}
static uint32_t word(uint32_t a){uint32_t x=0;void*p=ram(a,4);if(p)memcpy(&x,p,4);return x;}
static uint64_t fnv(const void*p,size_t n){uint64_t h=UINT64_C(14695981039346656037);const unsigned char*b=p;while(n--){h^=*b++;h*=UINT64_C(1099511628211);}return h;}
static void prng(uint32_t p[4]){for(unsigned i=0;i<4;i++)p[i]=word(0x2b16ec+4*i);}
static int identity(void){return ram(cp.owner,0x224)&&word(cp.owner)==0x1e3d98u&&word(0x390a54)==cp.model&&ram(cp.model,0x24)&&word(cp.model)==0x1eddbcu&&*(unsigned char*)ram(cp.model+0x18,1)==cp.gender;}
static int call(void(*fn)(void),uint32_t object,uint32_t sp,unsigned count,const uint32_t*args,uint32_t*out){
 uint32_t*p=ram(sp,4+count*4);if(!p)return 0;p[0]=0;for(unsigned i=0;i<count;i++)p[i+1]=args[i];
 g_esp=sp;g_ecx=object;fn();if(out)*out=g_eax;return g_esp==sp+4+count*4;
}
static int cdecl(void(*fn)(void),uint32_t sp,unsigned count,const uint32_t*args,uint32_t*out){
 uint32_t*p=ram(sp,4+count*4);if(!p)return 0;p[0]=0;for(unsigned i=0;i<count;i++)p[i+1]=args[i];
 g_esp=sp;fn();if(out)*out=g_eax;return g_esp==sp+4;
}
/* Original PAM stream decoding, same byte-count/cursor contract as existing
 * Creator diagnostic. No Python/host decompression or guessed stream layout. */
static int decode(uint32_t path,uint32_t sp,uint32_t buffer,unsigned char*out,uint32_t*size){
 uint32_t args[]={path,0x8000},stream=0,bytes=0;
 if(!cdecl(sub_000BC840,sp,2,args,&stream)||!ram(stream,0x24))return 0;
 if(!ram(word(stream),0x24)){uint32_t closearg[]={stream};(void)cdecl(sub_000BB680,sp,1,closearg,NULL);return 0;}
 uint32_t target=word(word(stream)+0x20);int ok=0;
 if(call(sub_000BD9F0,stream,sp,0,NULL,&bytes)&&bytes&&bytes<=ADN_MAX&&target>=0x10000u&&target<0x1d0000u){
  recomp_func_t fn=recomp_lookup(target);uint32_t readargs[]={buffer,bytes},got=0;
  if(fn&&word(stream+8)==0&&call(fn,stream,sp,2,readargs,&got)&&got==bytes&&word(stream+8)==bytes&&ram(buffer,bytes)){
   memcpy(out,ram(buffer,bytes),bytes);*size=bytes;ok=1;
  }
 }
 uint32_t closearg[]={stream};if(!cdecl(sub_000BB680,sp,1,closearg,NULL))ok=0;return ok;
}
static int fresh_path(const char*name,char*host,size_t cap,uint32_t path){
 int z=snprintf(host,cap,"%s/%s",cp.directory,name);struct stat st;if(z<0||(size_t)z>=cap||!lstat(host,&st)||errno!=ENOENT)return 0;
 const char*base=strrchr(cp.directory,'/');if(!base)return 0;
 z=snprintf(cp.guest,sizeof cp.guest,"Z:\\%s\\%s",base+1,name);if(z<0||(size_t)z>=sizeof cp.guest||!ram(path,(size_t)z+1))return 0;
 memcpy(ram(path,(size_t)z+1),cp.guest,(size_t)z+1);return 1;
}
static int encoded(const char*host,unsigned char**out,size_t*bytes){
 int fd=open(host,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;struct stat a,b;int ok=!fstat(fd,&a)&&S_ISREG(a.st_mode)&&a.st_uid==getuid()&&a.st_size>0&&a.st_size<=65536;
 unsigned char*p=ok?malloc((size_t)a.st_size):NULL;if(!p)ok=0;size_t at=0;
 while(ok&&at<(size_t)a.st_size){ssize_t n=read(fd,p+at,(size_t)a.st_size-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(ok&&(fstat(fd,&b)||a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||a.st_size!=b.st_size||a.st_mtim.tv_sec!=b.st_mtim.tv_sec||a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec||a.st_ctim.tv_sec!=b.st_ctim.tv_sec||a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec))ok=0;
 if(close(fd))ok=0;if(!ok){free(p);return 0;}*out=p;*bytes=at;return 1;
}
static int graph(uint32_t node,Node*out,unsigned*n,unsigned depth){
 if(depth>16||*n==GRAPH_MAX||!ram(node,0x80))return 0;
 for(unsigned i=0;i<*n;i++)if(out[i].va==node)return 1;
 uint32_t vt=word(node);if(!ram(vt,0x1ac))return 0;
 Node x={0};x.va=node;x.vt=vt;x.type=word(node+0x64);x.active=UINT32_MAX;x.raw_5c=word(node+0x5c);
 /* D3D90 getter delegates type16 through+10; never invoke a guest method. */
 uint32_t leaf=node;unsigned depth_active=0;
 while(depth_active++<16&&ram(leaf,0x80)&&word(word(leaf)+0x17c)==0xd3d90u){
  if(word(leaf+0x64)!=0x16u){x.active_node=leaf;x.active=*(unsigned char*)ram(leaf+0x7c,1);break;}
  uint32_t next=word(leaf+0x10);if(next==leaf)break;leaf=next;
 }
 /* Exact C9B70 -> C9B50 resolution. +5C is not the render-object field.
  * Index0 recurses in the original; diagnose it unresolved instead of guessing.
  *256 is an observer read budget, never a new gameplay validity condition. */
 x.descriptor=word(node+0xC);x.table=word(node+0x28);
 if(ram(x.descriptor,8))x.index=word(x.descriptor+4);
 if(x.index&&x.index<=256u&&ram(x.table,(size_t)x.index*4)){
  x.render=word(x.table+(x.index-1u)*4);x.render_valid=1;
  if(x.render){if(ram(x.render,32)){x.render_vt=word(x.render);memcpy(x.render_raw,ram(x.render,32),32);
   if(ram(x.render_vt,8))x.render_callback4=word(x.render_vt+4);else x.render_valid=0;
  }else x.render_valid=0;}
 }
 out[(*n)++]=x;uint32_t count=word(node+4),array=word(node+8);
 if(count>256u||!ram(array,(size_t)count*4))return !count;
 for(unsigned i=0;i<count;i++){uint32_t child=word(array+i*4);if(child&&!graph(child,out,n,depth+1))return 0;}return 1;
}
static int snapshot(Node*out,unsigned*n){
 *n=0;uint32_t count=word(cp.model+8),array=word(cp.model+12);if(!count||count>6||!ram(array,count*4))return 0;
 for(unsigned i=0;i<count;i++){uint32_t node=word(array+i*4);if(node&&!graph(node,out,n,0))return 0;}return 1;
}
void topspin_creator_model_checkpoint_observe(const char*phase){
 if(!enabled||!cp.active||cp.busy)return;VabGuestCpu cpu;vab_cpu_save(&cpu);cp.busy=1;
 Node now[GRAPH_MAX];unsigned n=0;int valid_identity=identity(),ok=valid_identity&&snapshot(now,&n);unsigned changed=0,logged=0;
 if(ok){for(unsigned i=0;i<cp.nodes;i++){unsigned j=0;for(;j<n;j++)if(now[j].va==cp.before[i].va)break;
  int different=j==n||memcmp(&cp.before[i],&now[j],sizeof(Node));if(different)changed++;
  if((different||!cp.observations)&&logged++<128){const Node*old=&cp.before[i];const Node*x=j==n?NULL:&now[j];
   fprintf(stderr,"[MODEL-CHECKPOINT-NODE] phase=%s va=%08x vt=%08x type=%u before_active=%u after_active=%u active_node=%08x raw5c=%08x/%08x desc=%08x/%08x index=%u/%u table=%08x/%08x render=%08x/%08x render_vt=%08x/%08x callback4=%08x/%08x render_valid=%u/%u render_raw=",
    phase?phase:"unknown",old->va,old->vt,old->type,old->active,x?x->active:UINT32_MAX,x?x->active_node:0,old->raw_5c,x?x->raw_5c:0,old->descriptor,x?x->descriptor:0,old->index,x?x->index:0,old->table,x?x->table:0,old->render,x?x->render:0,old->render_vt,x?x->render_vt:0,old->render_callback4,x?x->render_callback4:0,old->render_valid,x?x->render_valid:0);
   for(unsigned k=0;k<8;k++)fprintf(stderr,"%s%08x/%08x",k?",":"",old->render_raw[k],x?x->render_raw[k]:0);fputc('\n',stderr);
  }
 }}
 fprintf(stderr,"[MODEL-CHECKPOINT-GRAPH] phase=%s identity=%d valid=%d before=%u current=%u changed=%u observe_only=%d\n",phase?phase:"unknown",valid_identity,ok,cp.nodes,n,changed,observe_only);
 cp.observations++;cp.busy=0;vab_cpu_restore(&cpu);
}
int topspin_creator_model_checkpoint_configure(const char*root){
 if(configured)return 0;configured=1;const char*f=getenv("TOPSPIN_CREATOR_MODEL_CHECKPOINT"),*c=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");
 const char*o=getenv("TOPSPIN_CREATOR_MODEL_OBSERVE");observe_only=o&&!strcmp(o,"1");
 if((!f||strcmp(f,"1"))&&!observe_only)return 1;if(!c||strcmp(c,"1")||!root)return 0;
 if(observe_only){enabled=1;return 1;}
 char cache[PATH_MAX],actual[PATH_MAX];int n=snprintf(cache,sizeof cache,"%s/Cache",root);struct stat st;
 if(n<0||(size_t)n>=sizeof cache||!realpath(cache,actual)||strcmp(cache,actual)||lstat(cache,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid())return 0;
 memcpy(cache_root,cache,strlen(cache)+1);enabled=1;return 1;
}
void topspin_creator_model_checkpoint_discard(void){
 if(cp.busy)return;char host[PATH_MAX];if(cp.directory[0]&&!cp.retain){const char*names[]={"before.adn","after.adn"};for(unsigned i=0;i<2;i++){
  int n=snprintf(host,sizeof host,"%s/%s",cp.directory,names[i]);if(n>0&&(size_t)n<sizeof host)(void)unlink(host);
 }(void)rmdir(cp.directory);}free(cp.encoded);memset(&cp,0,sizeof cp);
}
int topspin_creator_model_checkpoint_begin(unsigned kind,uint32_t owner){
 if(!enabled||kind!=MC_SAVE_OPERATION_CREATOR)return 1;if(cp.active||cp.busy)return 0;
 cp.kind=kind;cp.owner=owner;cp.model=word(0x390a54);cp.gender=ram(cp.model+0x18,1)?*(unsigned char*)ram(cp.model+0x18,1):UINT32_MAX;
 if(!identity())return 0;
 if(observe_only){VabGuestCpu cpu;vab_cpu_save(&cpu);cp.nodes=0;int valid=snapshot(cp.before,&cp.nodes);cp.active=1;
  topspin_creator_model_checkpoint_observe("observe-begin");fprintf(stderr,"[MODEL-CHECKPOINT] observe-only model=%08x graph=%d/%u; no ADN IO, serialization, loader or restoration\n",cp.model,valid,cp.nodes);vab_cpu_restore(&cpu);return 1;}
 if(g_esp<0x15000u||g_esp>0x4000000u)return 0;
 VabGuestCpu cpu;vab_cpu_save(&cpu);uint32_t base=g_esp-0x4000,sp=base+0x1800,path=base+0x2600,buffer=base+0x2800;
 unsigned char stack[0x4000];memcpy(stack,ram(base,sizeof stack),sizeof stack);uint32_t before[4],after[4];prng(before);cp.nodes=0;int graph_ok=snapshot(cp.before,&cp.nodes);cp.busy=1;
 int n=snprintf(cp.directory,sizeof cp.directory,"%s/.topspin-model-XXXXXX",cache_root);int ok=n>0&&(size_t)n<sizeof cp.directory&&mkdtemp(cp.directory)!=NULL;
 char host[PATH_MAX];if(ok)ok=fresh_path("before.adn",host,sizeof host,path);uint32_t arg[]={path};
 if(ok)ok=call(sub_000C43F0,cp.model,sp,1,arg,NULL)&&decode(path,sp,buffer,cp.adn,&cp.size)&&identity()&&encoded(host,&cp.encoded,&cp.encoded_bytes);
 prng(after);ok=ok&&!memcmp(before,after,sizeof before);/* The reference graph is from BEFORE the serializer, so observation also
  * detects any serializer side effect; it never grants restore authority. */
 memcpy(ram(base,sizeof stack),stack,sizeof stack);vab_cpu_restore(&cpu);cp.busy=0;
 if(!ok){fprintf(stderr,"[MODEL-CHECKPOINT] capture rejected; original Save must not run\n");topspin_creator_model_checkpoint_discard();vab_cpu_restore(&cpu);return 0;}
 cp.active=1;topspin_creator_model_checkpoint_observe("after-capture");fprintf(stderr,"[MODEL-CHECKPOINT] captured model=%08x gender=%u bytes=%u fnv=%016"PRIx64" graph=%d/%u; no Save success\n",cp.model,cp.gender,cp.size,fnv(cp.adn,cp.size),graph_ok,cp.nodes);vab_cpu_restore(&cpu);return 1;
}
int topspin_creator_model_checkpoint_restore(unsigned kind,uint32_t owner){
 if(!enabled||kind!=MC_SAVE_OPERATION_CREATOR)return 1;
 if(!cp.active||cp.busy||kind!=cp.kind||owner!=cp.owner||!identity()||mc_save_operation_active()||g_esp<0x15000u||g_esp>0x4000000u)return 0;
 if(observe_only){topspin_creator_model_checkpoint_observe("observe-after-rollback");return 1;}
 topspin_creator_model_checkpoint_observe("before-restore");VabGuestCpu cpu;vab_cpu_save(&cpu);
 uint32_t base=g_esp-0x4000,sp=base+0x1800,path=base+0x2600,empty=base+0x2380,buffer=base+0x2800;unsigned char stack[0x4000],after_adn[ADN_MAX];uint32_t after_size=0,before[4],after[4];memcpy(stack,ram(base,sizeof stack),sizeof stack);prng(before);cp.busy=1;
 char host[PATH_MAX];int n=snprintf(host,sizeof host,"%s/before.adn",cp.directory);unsigned char*bytes=NULL;size_t length=0;
 int ok=n>0&&(size_t)n<sizeof host&&encoded(host,&bytes,&length)&&length==cp.encoded_bytes&&!memcmp(bytes,cp.encoded,length);free(bytes);
 if(ok){const char*dir=strrchr(cp.directory,'/');n=snprintf(cp.guest,sizeof cp.guest,"Z:\\%s\\before.adn",dir+1);ok=n>0&&(size_t)n<sizeof cp.guest;}
 if(ok){memcpy(ram(path,strlen(cp.guest)+1),cp.guest,strlen(cp.guest)+1);*(unsigned char*)ram(empty,1)=0;uint32_t args[]={path,empty,0},ret=0;ok=call(sub_000C4310,cp.model,sp,3,args,&ret)&&(ret&255u)==1&&identity();}
 if(ok)ok=fresh_path("after.adn",host,sizeof host,path);
 uint32_t arg[]={path};if(ok)ok=call(sub_000C43F0,cp.model,sp,1,arg,NULL)&&decode(path,sp,buffer,after_adn,&after_size)&&after_size==cp.size&&!memcmp(after_adn,cp.adn,cp.size)&&identity();
 prng(after);ok=ok&&!memcmp(before,after,sizeof before);memcpy(ram(base,sizeof stack),stack,sizeof stack);vab_cpu_restore(&cpu);cp.busy=0;
 fprintf(stderr,"[MODEL-CHECKPOINT] original reload verified=%d model=%08x gender=%u before_bytes=%u after_bytes=%u; no Save success or PRNG rollback\n",ok,cp.model,cp.gender,cp.size,after_size);
 topspin_creator_model_checkpoint_observe("after-restore");if(!ok){cp.retain=1;fprintf(stderr,"[MODEL-CHECKPOINT] retained rejected reload evidence=%s\n",cp.directory);}vab_cpu_restore(&cpu);return ok;
}
