/* A single original FFBC0 call and its FF0B0 apply inputs/outputs. djultra64. */
#include "replay_sample_capture.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#define SEGMENTS 32u
#define CONTEXT_BYTES 0xb20u
#define DESCRIPTOR_BYTES 0x150u
#define MAX_TRACKS 5u
/* OFF/armed/in-flight/ready/written, published with release/acquire ordering. */
enum {OFF,ARMED,BUSY,READY,WRITTEN};
struct Segment {char name[40];uint32_t va,offset,bytes;};
static struct {
 atomic_int phase;
 char prefix[3800];uint64_t start,end,time;
 unsigned char *data;uint32_t used,count,rejected,errors,apply_count,apply_after_count;
 const unsigned char *ram;uint32_t ram_bytes,context,descriptor,tracks,stack;
 uint32_t arguments[3],caller,interval,tick,ring_address,ring_count;
 uint32_t track_address[2];int apply_pending,entry_kind;
 struct Segment segment[SEGMENTS];
} cap;
static int range(uint32_t va,uint32_t size){return size&&va>=0x10000u&&va<=cap.ram_bytes&&size<=cap.ram_bytes-va;}
static uint32_t word(uint32_t va){uint32_t v;memcpy(&v,cap.ram+va,4);return v;}
static int segment(const char *name,uint32_t va,uint32_t bytes)
{
 if(!range(va,bytes)||cap.count==SEGMENTS||bytes>TOPSPIN_REPLAY_SAMPLE_BUDGET-cap.used){cap.errors++;return 0;}
 struct Segment*s=&cap.segment[cap.count++];snprintf(s->name,sizeof s->name,"%s",name);
 s->va=va;s->offset=cap.used;s->bytes=bytes;
 memcpy(cap.data+cap.used,cap.ram+va,bytes);cap.used+=bytes;return 1;
}
static int seconds(const char *key,unsigned fallback,uint64_t *out)
{
 const char*p=getenv(key);unsigned n=0;if(!p){*out=(uint64_t)fallback*1000000000u;return 1;}
 if(!*p)return 0;
 for(;*p;p++){if(*p<'0'||*p>'9'||n>8640u)return 0;n=n*10u+(unsigned)(*p-'0');if(n>86400u)return 0;}
 *out=(uint64_t)n*1000000000u;return 1;
}
int topspin_replay_sample_capture_init(void)
{
 const char*p=getenv("TOPSPIN_REPLAY_SAMPLE_PREFIX");if(!p)return 1;
 if(!*p||strlen(p)>=sizeof cap.prefix||!seconds("TOPSPIN_REPLAY_SAMPLE_START_SECONDS",60,&cap.start)||!seconds("TOPSPIN_REPLAY_SAMPLE_END_SECONDS",85,&cap.end)||cap.end<=cap.start)return 0;
 cap.data=malloc(TOPSPIN_REPLAY_SAMPLE_BUDGET);if(!cap.data)return 0;
 memcpy(cap.prefix,p,strlen(p)+1);atomic_store_explicit(&cap.phase,ARMED,memory_order_release);return 1;
}
int topspin_replay_sample_capture_enabled(void){return atomic_load_explicit(&cap.phase,memory_order_relaxed)==ARMED;}
int topspin_replay_sample_capture_wants(uint64_t now){return topspin_replay_sample_capture_enabled()&&now>=cap.start&&now<cap.end;}
static void destination(const char *suffix)
{
 char name[40];snprintf(name,sizeof name,"descriptor_%s",suffix);segment(name,cap.descriptor,DESCRIPTOR_BYTES);
 uint32_t table=range(cap.descriptor+0x48,4)?word(cap.descriptor+0x48):0;
 if(!range(table,32)){cap.errors++;return;}
 snprintf(name,sizeof name,"track_table_%s",suffix);segment(name,table,32);
 for(unsigned p=0;p<2;p++){
  uint32_t address=word(table+p*16+4);
  if(!strcmp(suffix,"before"))cap.track_address[p]=address;
  else if(address!=cap.track_address[p])cap.errors++;
  snprintf(name,sizeof name,"player%u_tracks_%s",p,suffix);segment(name,address,cap.tracks*0x34u);
 }
}
static int enter_kind(uint64_t now,uint32_t context,uint32_t stack,const void *base,uint32_t bytes,int kind)
{
 if(!topspin_replay_sample_capture_wants(now))return 0;
 /* The producer is the title's serialized CPU route. Foreign/reentrant calls
  * do not replace borrowed memory or the selected invocation. */
 int expected=ARMED;if(!atomic_compare_exchange_strong_explicit(&cap.phase,&expected,BUSY,memory_order_acq_rel,memory_order_relaxed))return 0;
 cap.ram=base;cap.ram_bytes=bytes;
 if(!base||!range(context,CONTEXT_BYTES)||!range(stack,kind?8:16)||!range(0x41346c,4)||!range(0x1b939c,4)||word(context)!=0x237c1cu||word(context+0xaa0)!=2u||word(0x1b939c)!=2u){cap.rejected++;atomic_store_explicit(&cap.phase,ARMED,memory_order_release);return 0;}
 uint32_t count=word(context+0xec),ring=word(context+0xe8),tracks=word(0x41346c);
 uint32_t descriptor=word(context+0x10);int32_t a=kind?0:(int32_t)word(stack+4),b=kind?0:(int32_t)word(stack+8);float alpha;uint32_t bits=kind?0:word(stack+12);memcpy(&alpha,&bits,4);
 if(kind){
  uint32_t index=word(context+0xa9c),slot=word(context+0xf0)-word(context+0xf4)+index;
  if((int32_t)slot<0)slot+=count;
  uint64_t expected_source=(uint64_t)ring+(uint64_t)slot*0x54u;
  if(word(stack)!=0x100554u||index>INT32_MAX||slot>=count||expected_source>UINT32_MAX||word(stack+4)!=(uint32_t)expected_source){cap.rejected++;atomic_store_explicit(&cap.phase,ARMED,memory_order_release);return 0;}
  a=b=(int32_t)index;bits=0;alpha=0;
 }
 /* Snapshot the full recorded allocation, not just the two indexed records.
  * Keep96 trailing bytes for the largest bounded decoded-track read. */
 if(!count||count>8192u||!tracks||tracks>MAX_TRACKS||!range(ring,count*0x54u+96u)||!range(descriptor,DESCRIPTOR_BYTES)||a< -1||b< -1||(a<0&&b<0)||(!kind&&(a>=(int32_t)count||b>=(int32_t)count))||!isfinite(alpha)||alpha<0||alpha>1){cap.rejected++;atomic_store_explicit(&cap.phase,ARMED,memory_order_release);return 0;}
 cap.entry_kind=kind;cap.time=now;cap.context=context;cap.descriptor=descriptor;cap.tracks=tracks;cap.stack=stack;cap.caller=word(stack);cap.arguments[0]=(uint32_t)a;cap.arguments[1]=(uint32_t)b;cap.arguments[2]=bits;cap.interval=word(0x1b939c);cap.tick=word(0x413284);cap.ring_address=ring;cap.ring_count=count;
 segment("context_before",context,CONTEXT_BYTES);segment("ring_before",ring,count*0x54u+96u);
 const uint32_t pages[]={0x222000,0x223000,0x227000,0x22e000,0x24a000};
 for(unsigned i=0;i<sizeof pages/sizeof pages[0];i++){char name[40];snprintf(name,sizeof name,"math_page_%08X",pages[i]);segment(name,pages[i],4096);}
 segment("globals_413000",0x413000,4096);
 destination("before");return 1;
}
int topspin_replay_sample_capture_enter(uint64_t now,uint32_t context,uint32_t stack,const void *base,uint32_t bytes)
{return enter_kind(now,context,stack,base,bytes,0);}
int topspin_replay_sample_capture_enter_apply(uint64_t now,uint32_t context,uint32_t stack,const void *base,uint32_t bytes)
{return enter_kind(now,context,stack,base,bytes,1);}
void topspin_replay_sample_capture_cpu(const TopspinReplayCpuState *state)
{
 if(atomic_load_explicit(&cap.phase,memory_order_acquire)!=BUSY||!state)return;
 if(cap.count==SEGMENTS||sizeof *state>TOPSPIN_REPLAY_SAMPLE_BUDGET-cap.used){cap.errors++;return;}
 struct Segment*s=&cap.segment[cap.count++];snprintf(s->name,sizeof s->name,"cpu_before");s->va=0;s->offset=cap.used;s->bytes=sizeof *state;
 memcpy(cap.data+cap.used,state,sizeof *state);cap.used+=(uint32_t)sizeof *state;
}
void topspin_replay_sample_capture_apply_before(uint32_t context,uint32_t stack)
{
 if(atomic_load_explicit(&cap.phase,memory_order_acquire)!=BUSY)return;
 cap.apply_count++;
 if(cap.apply_pending||context!=cap.context||!range(stack,8)||word(stack)!=(cap.entry_kind?0x100554u:0x10036du)){cap.errors++;return;}
 uint32_t source=word(stack+4),bytes=20u+16u*2u*cap.tracks;
 if(segment("sampler_output_before_apply",source,bytes))cap.apply_pending=1;
}
void topspin_replay_sample_capture_apply_after(void)
{
 if(atomic_load_explicit(&cap.phase,memory_order_acquire)!=BUSY||!cap.apply_pending)return;
 cap.apply_pending=0;cap.apply_after_count++;destination("after");
}
void topspin_replay_sample_capture_leave(void)
{
 if(atomic_load_explicit(&cap.phase,memory_order_acquire)!=BUSY)return;
 segment("context_after_sampler",cap.context,CONTEXT_BYTES);
 if(cap.apply_count!=1||cap.apply_after_count!=1||cap.apply_pending)cap.errors++;
 cap.ram=NULL;atomic_store_explicit(&cap.phase,READY,memory_order_release);
}
int topspin_replay_sample_capture_poll(uint64_t now,int force)
{
 int phase=atomic_load_explicit(&cap.phase,memory_order_acquire);
 if(phase==OFF||phase==WRITTEN||phase==BUSY)return 1;
 if(phase==ARMED&&!force&&now<cap.end)return 1;
 int expected=phase;if(!atomic_compare_exchange_strong(&cap.phase,&expected,WRITTEN))return 1;
 char path[4096];int ok=1;
 if(phase==READY){snprintf(path,sizeof path,"%s.bin",cap.prefix);FILE*f=fopen(path,"wb");if(!f)ok=0;else{if(fwrite(cap.data,1,cap.used,f)!=cap.used)ok=0;if(fclose(f))ok=0;}}
 snprintf(path,sizeof path,"%s.txt",cap.prefix);FILE*f=fopen(path,"w");if(!f)ok=0;else{
  fprintf(f,"author=djultra64\nversion=1 status=%s errors=%u rejected=%u bytes=%u budget=%u segments=%u\ntime_ns=%"PRIu64" context=%08X caller=%08X stack=%08X interval=%08X tick=%u\nindices=%d,%d alpha_bits=%08X ring=%08X ring_count=%u tracks_per_player=%u apply_calls=%u apply_after=%u\n",phase==READY?"captured":"window_missed",cap.errors,cap.rejected,cap.used,TOPSPIN_REPLAY_SAMPLE_BUDGET,cap.count,cap.time,cap.context,cap.caller,cap.stack,cap.interval,cap.tick,(int32_t)cap.arguments[0],(int32_t)cap.arguments[1],cap.arguments[2],cap.ring_address,cap.ring_count,cap.tracks,cap.apply_count,cap.apply_after_count);
  fprintf(f,"entry_kind=%s\n",cap.entry_kind?"record_apply":"sampler");
  for(unsigned i=0;i<cap.count;i++){struct Segment*s=&cap.segment[i];fprintf(f,"segment=%s address=%08X offset=%u bytes=%u\n",s->name,s->va,s->offset,s->bytes);}if(fclose(f))ok=0;
 }
 fprintf(stderr,"[REPLAY-SAMPLE] %s status=%s errors=%u bytes=%u\n",ok?"wrote":"IO-failed",phase==READY?"captured":"window_missed",cap.errors,cap.used);
 free(cap.data);cap.data=NULL;return ok;
}
