/* Own work: djultra64. Guest producer never waits for host file I/O. */
#include "lod_trace.h"
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#define CAP 64u
static TopspinLodSample samples[CAP];
static _Atomic unsigned published;
static _Atomic int enabled;
static atomic_flag producer=ATOMIC_FLAG_INIT;
static char path[1024];
static FILE *output;
static unsigned written;
static int failed,closed;
int topspin_lod_trace_init(void){
 const char*p=getenv("TOPSPIN_LOD_TRACE");
 if(!p||!*p)return 0;
 if(*p!='/'||strlen(p)>=sizeof path)return -1;
 memcpy(path,p,strlen(p)+1);atomic_store_explicit(&enabled,1,memory_order_release);return 0;
}
int topspin_lod_trace_enabled(void){return atomic_load_explicit(&enabled,memory_order_relaxed);}
static int same(const TopspinLodSample*a,const TopspinLodSample*b){
 return a->caller==b->caller&&a->owner==b->owner&&a->forced==b->forced&&a->thresholds==b->thresholds&&a->meshes==b->meshes&&a->count==b->count&&a->selected==b->selected&&!memcmp(a->mesh,b->mesh,sizeof a->mesh)&&!memcmp(a->threshold,b->threshold,sizeof a->threshold);
}
void topspin_lod_trace_sample(const TopspinLodSample*s){
 if(!topspin_lod_trace_enabled()||!s||!s->count||s->count>TOPSPIN_LOD_TIERS)return;
 if(atomic_flag_test_and_set_explicit(&producer,memory_order_acquire))return;
 unsigned n=atomic_load_explicit(&published,memory_order_relaxed);
 if(n<CAP){
  unsigned i;for(i=0;i<n;i++)if(same(s,&samples[i]))break;
  if(i==n){samples[n]=*s;atomic_store_explicit(&published,n+1,memory_order_release);}
 }
 atomic_flag_clear_explicit(&producer,memory_order_release);
}
int topspin_lod_trace_poll(int force){
 if(closed||!topspin_lod_trace_enabled())return failed?-1:0;
 unsigned n=atomic_load_explicit(&published,memory_order_acquire);
 if(n&&!output&&!failed){
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
  if(fd<0)failed=1;
  else if(!(output=fdopen(fd,"w"))){close(fd);failed=1;}
  if(output&&fputs("sample,wall_ns,guest_thread,caller,owner,forced,position_x_bits,position_y_bits,position_z_bits,thresholds,meshes,count,selected,tier,mesh,threshold_bits,complexity_known\n",output)<0)failed=1;
 }
 if(output&&!failed){
  for(;written<n;written++){
   const TopspinLodSample*s=&samples[written];
   for(unsigned j=0;j<s->count;j++)if(fprintf(output,"%u,%llu,%u,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%u,%u,%u,%08x,%08x,0\n",written,(unsigned long long)s->wall_ns,s->thread,s->caller,s->owner,s->forced,s->position[0],s->position[1],s->position[2],s->thresholds,s->meshes,s->count,s->selected,j,s->mesh[j],s->threshold[j])<0){failed=1;break;}
   if(failed)break;
  }
  if(fflush(output))failed=1;
 }
 if(force||failed||n==CAP){
  /* Unpublished in-flight producer data is never read. Shutdown integration
   * calls force after guest workers stop; early force simply discards later samples. */
  atomic_store_explicit(&enabled,0,memory_order_release);
  if(output&&fclose(output))failed=1;
  output=NULL;closed=1;
 }
 return failed?-1:0;
}
