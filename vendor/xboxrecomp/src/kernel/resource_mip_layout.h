/* Exact swizzled RGBA8 2D mip layout, checked against original XDK5558
 * GetSurfaceLevel/15E5A0 CPU bodies. Own work: djultra64. No GPU authority. */
#ifndef XBOX_RESOURCE_MIP_LAYOUT_H
#define XBOX_RESOURCE_MIP_LAYOUT_H
#include <stdint.h>
#include <string.h>
typedef struct {uint32_t level,width,height,pitch,bytes,data,bits,format;} RjpMip;
static inline int rjp_mip_at(const uint32_t parent[5],uint32_t level,RjpMip*out){
 if(!parent||!out||(parent[0]&0x70000u)!=0x40000u)return 0;
 /* Original15E5A0 linear Size branch: width/height minus1, pitch64 minus1.
  * One level only; copied rows include authentic padding, never swizzled. */
 if(((parent[3]>>8)&255u)==0x12u){
  uint32_t f=parent[3],z=parent[4],w=(z&4095u)+1u,h=((z>>12)&4095u)+1u,pitch=((z>>24)+1u)*64u;
  uint32_t raw=parent[1],bits=raw<0x4000000u?raw|0x80000000u:raw;
  if(level||!z||(f&4u)||(f&0xf0u)!=0x20u||((f>>16)&15u)!=1u||(f&0xfff00000u)||
     w>512u||h>512u||pitch<w*4u||pitch>2048u||!raw||bits<0x80000000u||(uint64_t)bits+(uint64_t)pitch*h>0x84000000ull)return 0;
  RjpMip q={0,w,h,pitch,pitch*h,raw,bits,f};*out=q;return 1;
 }
 if(parent[4])return 0;
 uint32_t f=parent[3],wx=(f>>20)&15u,hy=(f>>24)&15u,count=(f>>16)&15u;
 if((f&4u)||(f&0xf0u)!=0x20u||((f>>8)&255u)!=6u||wx>9||hy>9||!count||count>(wx>hy?wx:hy)+1||level>=count)return 0;
 uint32_t raw=parent[1],base=raw<0x4000000u?raw|0x80000000u:raw;
 if(!raw||base<0x80000000u||base>=0x84000000u)return 0;
 uint32_t offset=0;for(uint32_t i=0;i<level;i++)offset+=(1u<<(wx>i?wx-i:0))*(1u<<(hy>i?hy-i:0))*4u;
 uint32_t x=wx>level?wx-level:0,y=hy>level?hy-level:0;
 RjpMip q={level,1u<<x,1u<<y,(1u<<x)*4u,(1u<<x)*(1u<<y)*4u,raw+offset,base+offset,(f&~0x0ff00000u)|(x<<20)|(y<<24)};
 if((uint64_t)q.bits+q.bytes>0x84000000ull)return 0;
 *out=q;return 1;
}
static inline int rjp_mip_surface(uint32_t parent_resource,const uint32_t parent[5],const uint32_t surface[6],RjpMip*out){
 if(!parent_resource||!parent||!surface||!out||(surface[0]&0x70000u)!=0x50000u||surface[5]!=parent_resource)return 0;
 if(((parent[3]>>8)&255u)==0x12u){RjpMip q;if(!rjp_mip_at(parent,0,&q)||surface[1]!=q.data||surface[3]!=q.format||surface[4]!=parent[4])return 0;*out=q;return 1;}
 if(surface[4])return 0;
 uint32_t count=(parent[3]>>16)&15u;for(uint32_t i=0;i<count;i++){RjpMip q;if(!rjp_mip_at(parent,i,&q))return 0;
  if(surface[1]==q.data&&surface[3]==q.format){*out=q;return 1;}}
 return 0;
}
#endif
