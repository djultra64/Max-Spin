/* Actual allocator/epoch source, startup environment observations. djultra64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
static atomic_uint contig_queries;
static char*observed_getenv(const char*name){
 if(!strcmp(name,"RECOMP_CONTIG_RECLAIM"))atomic_fetch_add(&contig_queries,1);
 return getenv(name);
}
#define getenv observed_getenv
#include "kernel/xbox_memory_layout.c"
#undef getenv
static void*lookup(void*unused){
 (void)unused;
 for(unsigned i=0;i<10000;i++){
  uint32_t base=123,size=456;uint64_t epoch=789;
  if(!xbox_ContiguousEpoch(0x80001000,1,&base,&size,&epoch))assert(base==123&&size==456&&epoch==789);
 }
 return NULL;
}
int main(int argc,char**argv){
 assert(argc==2);int mode=atoi(argv[1]);
 unsetenv("RECOMP_HEAP_RECLAIM");unsetenv("RECOMP_CONTIG_RECLAIM");
 if(mode==1||mode==2)setenv("RECOMP_CONTIG_RECLAIM","1",1);
 if(mode==3)setenv("RECOMP_HEAP_RECLAIM","1",1);
 void*memory=calloc(1,XBOX_CONTIG_SIZE);assert(memory);
 g_memory_offset=(ptrdiff_t)((uintptr_t)memory-XBOX_CONTIG_BASE);
 uint32_t base=0,size=0;uint64_t epoch=0;
 if(mode!=2){
  uint32_t a=xbox_ContiguousAlloc(8192,4096);assert(a);
  if(mode){
   assert(xbox_ContiguousEpoch(a+4000,4096,&base,&size,&epoch)&&base==a&&size==8192&&epoch);
   uint64_t old=epoch;assert(xbox_ContiguousFree(a));
   assert(!xbox_ContiguousEpoch(a,1,&base,&size,&epoch));
   assert(xbox_ContiguousAlloc(4096,4096)==a);
   assert(xbox_ContiguousEpoch(a,4096,&base,&size,&epoch)&&epoch>old);
   assert(!xbox_ContiguousEpoch(a+4095,2,&base,&size,&epoch));
  }else assert(!xbox_ContiguousEpoch(a,1,&base,&size,&epoch));
 }
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,lookup,NULL));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 assert(atomic_load(&contig_queries)==1);
 free(memory);puts("PASS actual allocator: cached startup switch, concurrent queries, live free/reuse epochs and span bounds unchanged");return 0;
}
