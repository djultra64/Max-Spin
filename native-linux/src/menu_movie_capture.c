/* Bounded post-original source capture; disabled unless an explicit absolute
 * TOPSPIN_MOVIE_CAPTURE_PREFIX is supplied. Never reads GPU or calls guest
 * callbacks. Only diagnostic files opened O_EXCL0600 by host poll.
 * Own integration: djultra64. */
#include "menu_movie_capture.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
extern ptrdiff_t g_xbox_mem_offset;
extern uint64_t SDL_GetTicksNS(void);
extern uint32_t xbox_ContiguousAllocatedBytes(void);
#define CAPTURE_SLOTS 3
#define FRAME_BYTES (640u*480u*2u)
typedef struct {
 _Atomic int state; /*0unused,3copying,1ready,2hostowned,4done*/
 uint32_t width,height,pitch,format,size,raw_state,raw_counter,raw_schedule;
 uint64_t hash;
 unsigned char data[FRAME_BYTES];
} Capture;
static Capture slots[CAPTURE_SLOTS];
static _Atomic int configured=-1;
static _Atomic uint64_t host_ns;
static uint64_t minimum_ns;
static uint32_t wanted_height=350;
static char prefix[768];
static int configure(void){
 int ready=atomic_load_explicit(&configured,memory_order_acquire);if(ready!=-1)return ready==1;
 int expected=-1;if(!atomic_compare_exchange_strong(&configured,&expected,-2))return 0;
 const char*p=getenv("TOPSPIN_MOVIE_CAPTURE_PREFIX");
 if(!p||p[0]!='/'||strlen(p)>700){atomic_store_explicit(&configured,0,memory_order_release);return 0;}
 const char*size=getenv("TOPSPIN_MOVIE_CAPTURE_SIZE");
 if(size&&strcmp(size,"640x350")&&strcmp(size,"640x480")){atomic_store_explicit(&configured,0,memory_order_release);return 0;}
 if(size&&!strcmp(size,"640x480"))wanted_height=480;
 const char*ms=getenv("TOPSPIN_MOVIE_CAPTURE_MIN_MS");
 if(ms){
  unsigned value=0;const char*q=ms;
  if(!*q){atomic_store_explicit(&configured,0,memory_order_release);return 0;}
  for(;*q;q++){
   if(*q<'0'||*q>'9'||value>180000u/10u){atomic_store_explicit(&configured,0,memory_order_release);return 0;}
   value=value*10u+(unsigned)(*q-'0');
   if(value>180000u){atomic_store_explicit(&configured,0,memory_order_release);return 0;}
  }
  minimum_ns=(uint64_t)value*UINT64_C(1000000);
 }
 memcpy(prefix,p,strlen(p)+1);atomic_store_explicit(&configured,1,memory_order_release);return 1;
}
static int range(uint32_t p,size_t n){return p>=0x10000u&&(uint64_t)p+n<=0x4000000u;}
static int pixel_range(uint32_t p,size_t n){return range(p,n)||(p>=0x80010000u&&(uint64_t)p+n<=0x84000000u);}
static int read_at(uint32_t p,void*out,size_t n){if(!range(p,n))return 0;memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+p),n);return 1;}
static int name_is_video(uint32_t node){uint32_t p,n;return read_at(node+8,&p,4)&&p>=0x10010u&&read_at(p-12,&n,4)&&n==8&&range(p,9)&&!memcmp((void*)((uintptr_t)g_xbox_mem_offset+p),"ph_video",9);}
void topspin_menu_movie_capture_after(uint32_t node,uint32_t stack){
 if(!configure())return;
 if(minimum_ns&&atomic_load_explicit(&host_ns,memory_order_relaxed)<minimum_ns)return;
 int vacant=0;for(unsigned i=0;i<CAPTURE_SLOTS;i++)if(!atomic_load_explicit(&slots[i].state,memory_order_relaxed))vacant=1;
 if(!vacant)return;
 uint32_t args[2],klass,video,info[20],header[5];
 if(!range(node,0xe0)||!read_at(stack,args,8)||args[0]!=0x28a3a||!read_at(node,&klass,4)||klass!=0x1ee1e0||!name_is_video(node)
    ||!read_at(node+0x34,&video,4)||!read_at(video,info,sizeof info)||info[5]!=640||info[6]!=wanted_height||!read_at(info[19],header,sizeof header))return;
 if((header[0]&0x70000u)!=0x40000u||((header[3]>>8)&255u)!=0x24u)return;
 uint32_t width=(header[4]&0xfffu)+1,height=((header[4]>>12)&0xfffu)+1,pitch=((header[4]>>24)+1u)*64u;
 if(width!=640||height!=wanted_height||pitch<width*2u||pitch>4096)return;
 /* Resource data is a physical offset or its contiguous alias. Preserve an
  * existing alias; the low virtual window can contain different guest bytes. */
 uint32_t data=header[1];
 if(data>=0x10000u&&data<0x4000000u&&data<xbox_ContiguousAllocatedBytes())data|=0x80000000u;
 if(!pixel_range(data,(size_t)pitch*(height-1)+width*2u))return;
 for(unsigned i=0;i<CAPTURE_SLOTS;i++){
  int expected=0;if(!atomic_compare_exchange_strong(&slots[i].state,&expected,3))continue;
  Capture*s=&slots[i];s->width=width;s->height=height;s->pitch=pitch;s->format=header[3];s->size=header[4];s->raw_state=info[1];s->raw_counter=info[2];s->raw_schedule=info[8];
  for(unsigned y=0;y<height;y++)memcpy(s->data+(size_t)y*width*2u,(void*)((uintptr_t)g_xbox_mem_offset+data+(size_t)y*pitch),width*2u);
  uint64_t h=UINT64_C(14695981039346656037);for(size_t n=0;n<(size_t)width*height*2u;n++)h=(h^s->data[n])*UINT64_C(1099511628211);s->hash=h;
  atomic_store_explicit(&s->state,1,memory_order_release);return;
 }
}
static int write_all(int fd,const void*data,size_t n){const unsigned char*p=data;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;p+=k;n-=(size_t)k;}return 1;}
void topspin_menu_movie_capture_poll(void){
 if(!configure())return;
 /* Only the host poll reads a clock, and only for this explicit diagnostic.
  * This is an SDL wall-clock gate, not a movie PTS or guest schedule. */
 if(minimum_ns)atomic_store_explicit(&host_ns,SDL_GetTicksNS(),memory_order_relaxed);
 for(unsigned i=0;i<CAPTURE_SLOTS;i++){
  int expected=1;if(!atomic_compare_exchange_strong_explicit(&slots[i].state,&expected,2,memory_order_acquire,memory_order_relaxed))continue;
  Capture*s=&slots[i];uint32_t bytes=s->width*s->height*2u;char raw[832],meta[832],json[1024];int r=-1,m=-1,ok=0;
  snprintf(raw,sizeof raw,"%s.movie-%u.yuy2",prefix,i);snprintf(meta,sizeof meta,"%s.movie-%u.json",prefix,i);
  r=open(raw,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(r<0)goto done;
  m=open(meta,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(m<0)goto done;
  int n=snprintf(json,sizeof json,"{\"version\":1,\"author\":\"djultra64\",\"format\":\"yuyv422\",\"width\":%u,\"height\":%u,\"packed_row_bytes\":%u,\"guest_pitch\":%u,\"guest_format\":%u,\"guest_size\":%u,\"bytes\":%u,\"fnv1a64\":\"%016llx\",\"raw_movie_state\":%u,\"raw_movie_counter\":%u,\"raw_movie_schedule\":%u,\"pts_known\":false}\n",s->width,s->height,s->width*2,s->pitch,s->format,s->size,bytes,(unsigned long long)s->hash,s->raw_state,s->raw_counter,s->raw_schedule);
  ok=n>0&&(size_t)n<sizeof json&&write_all(r,s->data,bytes)&&write_all(m,json,(size_t)n);
 done:
  if(r>=0&&close(r))ok=0;
  if(m>=0&&close(m))ok=0;
  if(!ok){if(r>=0)unlink(raw);if(m>=0)unlink(meta);}
  if(ok)fprintf(stderr,"[MOVIE-CPU-CAPTURE] slot=%u bytes=%u complete pts_known=0\n",i,bytes);
  if(!ok)fprintf(stderr,"[MOVIE-CPU-CAPTURE] slot=%u output failed; existing files never replaced\n",i);
  atomic_store_explicit(&s->state,4,memory_order_release);
 }
}
