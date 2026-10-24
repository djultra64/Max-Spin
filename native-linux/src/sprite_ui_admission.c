#define _POSIX_C_SOURCE 200809L
#include "sprite_ui_admission.h"
#include "menu_pc_sha256.h"
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static const struct {const char*name,*sha;uint32_t w,h;} specs[3]={
 {"pc_cross.xpr","f6f4546c6279547e78e6acfdc4db700ca495b8c3c8c2e78ae1ef0664f9ad4bec",16,16},
 {"readystart.xpr","c9ead8ebee0a06da395fdfcc0353a9b94a2827753d0c7ef27397d3ea752d417d",64,16},
 {"shop_slider.xpr","4d41ea0dd70b2b885346b46d145e517aebe4c07b43ce10765a6b7b279bf0d07f",128,32}
};
typedef struct {unsigned char xpr[8192];size_t bytes;} Source;
typedef struct {uint32_t object,data,profile;} Live;
static struct {int ready;Source src[3];Live live[32];SxUiMap map;void*user;} state;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static uint32_t word(const void*p){uint32_t n;memcpy(&n,p,4);return n;}
static int load(int dir,unsigned i,Source*out){
 int fd=openat(dir,specs[i].name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;
 struct stat a,b;int ok=0;if(fstat(fd,&a)||!S_ISREG(a.st_mode)||a.st_uid!=getuid()||(a.st_mode&022)||a.st_size<32||(uint64_t)a.st_size>sizeof out->xpr)goto end;
 size_t n=(size_t)a.st_size,at=0;while(at<n){ssize_t r=read(fd,out->xpr+at,n-at);if(r<=0)goto end;at+=(size_t)r;}
 if(fstat(fd,&b)||a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||a.st_size!=b.st_size||a.st_mtim.tv_sec!=b.st_mtim.tv_sec||a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec||a.st_ctim.tv_sec!=b.st_ctim.tv_sec||a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec)goto end;
 if(!menu_pc_sha_is(out->xpr,n,specs[i].sha)||word(out->xpr)!=0x30525058||word(out->xpr+4)!=n||word(out->xpr+8)!=32)goto end;
 out->bytes=n;ok=1;
end:close(fd);return ok;
}
int sxui_configure(const char*path,const char*image,SxUiMap map,void*user){
#ifdef _WIN32
 if(!path||!(((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))&&path[1]==':'&&(path[2]=='/'||path[2]=='\\'))||!image||strcmp(image,"menu")||!map)return 0;
#else
 if(!path||path[0]!='/'||!image||strcmp(image,"menu")||!map)return 0;
#endif
 int dir=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 0;struct stat st;Source sources[3];int ok=0;
 if(fstat(dir,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&022))goto end;
 for(unsigned i=0;i<3;i++)if(!load(dir,i,&sources[i]))goto end;
 pthread_mutex_lock(&mutex);if(!state.ready){memcpy(state.src,sources,sizeof sources);state.map=map;state.user=user;state.ready=1;ok=1;}pthread_mutex_unlock(&mutex);
end:close(dir);return ok;
}
static void retire(uint32_t object){for(unsigned i=0;i<32;i++)if(state.live[i].object==object)memset(&state.live[i],0,sizeof state.live[i]);}
void sxui_retire(uint32_t object){pthread_mutex_lock(&mutex);retire(object);pthread_mutex_unlock(&mutex);}
void sxui_loaded(uint32_t object){
 pthread_mutex_lock(&mutex);retire(object);if(!state.ready)goto end;
 const unsigned char*r=state.map(state.user,object,0x32);
 if(!r||word(r)!=0x1fc6fc||!r[0x30]||r[0x31]||object>UINT32_MAX-16||word(r+12)!=object+16)goto end;
 const unsigned char*h=r+16;uint32_t physical=word(h+4)&0x0fffffffu;if((word(h)&0x70000)!=0x40000)goto end;
 for(unsigned i=0;i<3;i++){
  const Source*s=&state.src[i];if(word(h+12)!=word(s->xpr+24)||word(h+16)!=word(s->xpr+28))continue;
  const void*p=state.map(state.user,physical|0x80000000u,s->bytes-32);
  if(!p||memcmp(p,s->xpr+32,s->bytes-32))continue;
  for(unsigned n=0;n<32;n++)if(!state.live[n].object){state.live[n]=(Live){object,physical,i+1};goto end;}
 }
end:pthread_mutex_unlock(&mutex);
}
uint32_t sxui_match(uint32_t address,uint32_t format,uint32_t w,uint32_t h,uint32_t levels,const void*encoded,size_t bytes){
 if(format!=0x0e||levels!=1||!encoded||!((w==16&&h==16)||(w==64&&h==16)||(w==128&&h==32)))return 0;
 uint32_t result=0,physical=address&0x0fffffffu;pthread_mutex_lock(&mutex);
 if(state.ready)for(unsigned i=0;i<32;i++){
  unsigned p=state.live[i].profile;if(!p||state.live[i].data!=physical||specs[p-1].w!=w||specs[p-1].h!=h)continue;
  const Source*s=&state.src[p-1];if(bytes==s->bytes-32&&!memcmp(encoded,s->xpr+32,bytes)){result=p;break;}
 }
 pthread_mutex_unlock(&mutex);return result;
}

int sxui_available(void){pthread_mutex_lock(&mutex);int result=state.ready;pthread_mutex_unlock(&mutex);return result;}
