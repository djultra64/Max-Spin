/* Owned RGBA snapshot -> Xbox A8R8G8B8 Morton32. djultra64. */
#include "material_coherence.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static int power2(uint32_t n){return n&&!(n&(n-1));}
int mc_same_lock(const McLock*a,const McLock*b){
 if(!a||!b)return 0;
 return a->memory_epoch==b->memory_epoch&&a->resource==b->resource&&a->data==b->data&&a->bits==b->bits&&a->format==b->format&&a->width==b->width&&a->height==b->height&&a->pitch==b->pitch&&a->face==b->face&&a->level==b->level&&a->caller==b->caller&&!memcmp(a->header,b->header,sizeof a->header)&&!memcmp(a->parent_header,b->parent_header,sizeof a->parent_header);
}
static size_t morton(uint32_t x,uint32_t y,uint32_t w,uint32_t h){
 size_t at=0,bit=1;
 for(uint32_t mask=1;mask<w||mask<h;mask<<=1){
  if(mask<w){if(x&mask)at|=bit;bit<<=1;}
  if(mask<h){if(y&mask)at|=bit;bit<<=1;}
 }
 return at;
}
int mc_snapshot_matches(const McLock*b,const McLock*c,const McSnapshot*s,void*dst,size_t capacity){
 if(!b||!c||!s||!dst||!s->rgba)return 0;
 if(!mc_same_lock(b,c)||!mc_same_lock(b,&s->lock))return 0;
 if(b->face||b->level>9||!b->resource||!b->data||!b->bits)return 0;
 if(((b->header[3]>>8)&255)!=b->format||b->header[1]!=b->data)return 0;
 if(b->format==6){
  if(!mc_encoder_source_caller(b->caller)||!power2(b->width)||!power2(b->height)||b->width>512||b->height>512||b->pitch!=b->width*4)return 0;
 }else if(b->format==18){
  uint32_t z=b->header[4];
  if(!mc_linear_source_caller(b->caller)||b->level||!z||!b->width||!b->height||b->width>512||b->height>512||
     ((b->header[3]>>16)&15u)!=1u||(z&4095u)+1u!=b->width||((z>>12)&4095u)+1u!=b->height||
     ((z>>24)+1u)*64u!=b->pitch||b->pitch<b->width*4u||b->pitch>2048u)return 0;
 }else return 0;
 size_t bytes=(size_t)b->pitch*b->height;
 if(capacity<bytes||s->bytes!=bytes||s->layout!=MC_RGBA8_LINEAR)return 0;
 if((s->flags&15u)!=15u||!s->allocation||!s->contents||!s->guest_generation||!s->producer_serial)return 0;
 if(s->allocation!=s->observed_allocation||s->contents!=s->observed_contents||s->guest_generation!=s->observed_guest_generation||s->producer_serial>s->completed_serial)return 0;
 uintptr_t a=(uintptr_t)dst,r=(uintptr_t)s->rgba;
 if(a>UINTPTR_MAX-bytes||r>UINTPTR_MAX-bytes||(a<r+bytes&&r<a+bytes))return 0;
 return 1;
}
int mc_encode_rgba_morton(const unsigned char*src,uint32_t w,uint32_t h,void*dst,size_t capacity){
 if(!src||!dst||!power2(w)||!power2(h)||w>512||h>512||capacity<(size_t)w*h*4)return 0;
 size_t bytes=(size_t)w*h*4;uintptr_t a=(uintptr_t)dst,r=(uintptr_t)src;
 if(a>UINTPTR_MAX-bytes||r>UINTPTR_MAX-bytes||(a<r+bytes&&r<a+bytes))return 0;
 for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
  const unsigned char*p=src+((size_t)y*w+x)*4;unsigned char*q=(unsigned char*)dst+morton(x,y,w,h)*4;
  q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
 }
 return 1;
}
int mc_commit(const McLock*b,const McLock*c,const McSnapshot*s,void*dst,size_t capacity){
 if(!mc_snapshot_matches(b,c,s,dst,capacity))return 0;
 size_t bytes=(size_t)b->pitch*b->height;
 unsigned char*staging=malloc(bytes);if(!staging)return 0;
 if(b->format==6){if(!mc_encode_rgba_morton(s->rgba,b->width,b->height,staging,bytes)){free(staging);return 0;}}
 else{
  /* GPU readback uses the guest row stride; padding is not image content.
   * Preserve original destination padding and every alpha byte exactly. */
  memcpy(staging,dst,bytes);
  for(uint32_t y=0;y<b->height;y++)for(uint32_t x=0;x<b->width;x++){
   size_t at=(size_t)y*b->pitch+4u*x;const unsigned char*p=s->rgba+at;unsigned char*q=staging+at;
   q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
  }
 }
 if(!mc_same_lock(b,c)){free(staging);return 0;}
 memcpy(dst,staging,bytes);free(staging);return 1;
}
