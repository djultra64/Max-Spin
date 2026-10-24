/* One-event black-frame triplet from real Vulkan readback. Own work: djultra64. */
#include "black_frame_watch.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le16(unsigned char*p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void le32(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int seconds(const char*name,double fallback,uint64_t*out){
 const char*s=getenv(name);char*end;double value=fallback;
 if(s){errno=0;value=strtod(s,&end);if(end==s||*end||errno)return 0;}
 if(!isfinite(value)||value<0||value>=(double)UINT64_MAX/1e9)return 0;
 *out=(uint64_t)(value*1e9);return 1;
}
/* Backbuffer LockRect returns RGBA in this Vulkan runtime. BMP stores BGRA;
 * preserve all four bytes and top-down row order, with no image processing. */
static int bmp(const char*path,unsigned width,unsigned height,const unsigned char*rgba,size_t pitch){
 uint64_t rowbytes=(uint64_t)width*4,bytes=rowbytes*height;
 unsigned char header[54]={0},*row;char temporary[1200];int ok=1;
 if(!width||!height||width>INT32_MAX||height>INT32_MAX||bytes>UINT32_MAX-54||pitch<rowbytes)return 0;
 if(snprintf(temporary,sizeof temporary,"%s.tmp",path)>=(int)sizeof temporary)return 0;
 row=malloc((size_t)rowbytes);if(!row)return 0;
 FILE*f=fopen(temporary,"wb");if(!f){free(row);return 0;}
 header[0]='B';header[1]='M';le32(header+2,(uint32_t)bytes+54);le32(header+10,54);
 le32(header+14,40);le32(header+18,width);le32(header+22,0u-height);le16(header+26,1);le16(header+28,32);le32(header+34,(uint32_t)bytes);
 if(fwrite(header,1,54,f)!=54)ok=0;
 for(unsigned y=0;ok&&y<height;y++){
  const unsigned char*src=rgba+(size_t)y*pitch;
  for(unsigned x=0;x<width;x++){row[x*4]=src[x*4+2];row[x*4+1]=src[x*4+1];row[x*4+2]=src[x*4];row[x*4+3]=src[x*4+3];}
  if(fwrite(row,1,(size_t)rowbytes,f)!=(size_t)rowbytes)ok=0;
 }
 if(fclose(f))ok=0;free(row);if(ok&&rename(temporary,path))ok=0;if(!ok)remove(temporary);return ok;
}
static struct{
 int initialized,enabled,await_after,have_previous,previous_dark;
 char prefix[768];uint64_t start,end,previous_ns,previous_frame;
 unsigned width,height;unsigned char*previous;size_t capacity;
} watch;
static void stop(void){watch.enabled=0;free(watch.previous);watch.previous=NULL;watch.capacity=0;}
static void save(const char*role,uint64_t ns,uint64_t frame,unsigned width,unsigned height,const unsigned char*rgba,size_t pitch){
 char path[1100];int n=snprintf(path,sizeof path,"%s-%s-%llu-ns-f%llu.bmp",watch.prefix,role,(unsigned long long)ns,(unsigned long long)frame);
 int ok=n>0&&n<(int)sizeof path&&bmp(path,width,height,rgba,pitch);
 fprintf(stderr,"[BLACK-WATCH] %s %s role=%s elapsed_ns=%llu frame=%llu size=%ux%u\n",ok?"saved":"FAILED",path,role,(unsigned long long)ns,(unsigned long long)frame,width,height);
}
int topspin_black_frame_watch_after_present(IDirect3DDevice8*device,uint64_t elapsed_ns,uint64_t frame){
 if(!watch.initialized){
  watch.initialized=1;const char*p=getenv("TOPSPIN_BLACK_WATCH_PREFIX");if(!p||!*p)return 0;
  if(strlen(p)>=sizeof watch.prefix||!seconds("TOPSPIN_BLACK_WATCH_START",10,&watch.start)||
     !seconds("TOPSPIN_BLACK_WATCH_END",100,&watch.end)||watch.end<watch.start){
   fputs("[BLACK-WATCH] invalid prefix/window; disabled\n",stderr);return 0;
  }
  memcpy(watch.prefix,p,strlen(p)+1);watch.enabled=1;
 }
 if(!watch.enabled||!device||elapsed_ns<watch.start)return 0;
 if(!watch.await_after&&elapsed_ns>watch.end){stop();return 0;}
 IDirect3DSurface8*surface=NULL;D3DSURFACE_DESC desc={0};D3DLOCKED_RECT lock={0};
 int locked=0,event=0,failed=0;
 HRESULT hr=device->lpVtbl->GetBackBuffer(device,0,0,&surface);
 if(hr<0||!surface){failed=1;goto done;}
 hr=surface->lpVtbl->GetDesc(surface,&desc);if(hr<0){failed=1;goto done;}
 /* Bounded memory even if a damaged descriptor reaches the diagnostic. The
  * normal title image is 640x480; higher configured render scales are kept
  * at their actual dimensions, never rescaled for the saved evidence. */
 if(desc.Format!=D3DFMT_A8R8G8B8||!desc.Width||!desc.Height||desc.Width>4096||desc.Height>4096){failed=1;goto done;}
 hr=surface->lpVtbl->LockRect(surface,&lock,NULL,D3DLOCK_READONLY);if(hr<0){failed=1;goto done;}locked=1;
 if(!lock.pBits||lock.Pitch<0||(uint64_t)lock.Pitch<(uint64_t)desc.Width*4){failed=1;goto done;}
 if(watch.await_after){
  save("after",elapsed_ns,frame,desc.Width,desc.Height,lock.pBits,(size_t)lock.Pitch);
  stop();goto done;
 }
 size_t bytes=(size_t)desc.Width*desc.Height*4;
 if(watch.capacity!=bytes){
  unsigned char*next=realloc(watch.previous,bytes);if(!next){failed=1;goto done;}
  watch.previous=next;watch.capacity=bytes;watch.have_previous=0;
 }
 if(watch.width!=desc.Width||watch.height!=desc.Height)watch.have_previous=0;
 uint64_t dark=0,total=(uint64_t)desc.Width*desc.Height;
 for(unsigned y=0;y<desc.Height;y++){
  const unsigned char*row=(const unsigned char*)lock.pBits+(size_t)y*lock.Pitch;
  for(unsigned x=0;x<desc.Width;x++)if(row[x*4]<8&&row[x*4+1]<8&&row[x*4+2]<8)dark++;
 }
 int is_dark=dark*100>=total*98;
 /* A leading loading/initial black image cannot consume the one event. Keep
  * the preceding actually observed nonblack frame for a useful transition. */
 if(is_dark&&watch.have_previous&&!watch.previous_dark){
  save("before",watch.previous_ns,watch.previous_frame,watch.width,watch.height,watch.previous,(size_t)watch.width*4);
  save("dark",elapsed_ns,frame,desc.Width,desc.Height,lock.pBits,(size_t)lock.Pitch);
  fprintf(stderr,"[BLACK-WATCH] event elapsed_ns=%llu frame=%llu dark_pixels=%llu total_pixels=%llu; next frame completes capture\n",
   (unsigned long long)elapsed_ns,(unsigned long long)frame,(unsigned long long)dark,(unsigned long long)total);
  watch.await_after=1;event=1;goto done;
 }
 for(unsigned y=0;y<desc.Height;y++)memcpy(watch.previous+(size_t)y*desc.Width*4,(const unsigned char*)lock.pBits+(size_t)y*lock.Pitch,(size_t)desc.Width*4);
 watch.width=desc.Width;watch.height=desc.Height;watch.previous_ns=elapsed_ns;watch.previous_frame=frame;watch.previous_dark=is_dark;watch.have_previous=1;
done:
 if(locked)surface->lpVtbl->UnlockRect(surface);
 if(surface)surface->lpVtbl->Release(surface);
 if(failed){fprintf(stderr,"[BLACK-WATCH] readback failed api=%08X; disabled\n",(unsigned)hr);stop();}
 return event;
}
