/*
   
  *******  Super XBR Shader - pass0  *******
   
  Copyright (c) 2015 Hyllian - sergiogdb@gmail.com

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/
/* CPU adaptation of Hyllian Super-xBR passes0/1/2, original revision
 * libretro/glsl-shaders30bb6f3a05b17571ece58f5e3b097f5257586952.
 * Own adapter/alpha/ownership work: djultra64. See sprite_xbr.h for fixed profile.
 * No game assets, SDK state, I/O or GPU dependencies. */
#include "sprite_xbr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {float v[4];} Pixel;
typedef struct {Pixel *p;uint32_t w,h;size_t bytes;} FloatImage;
typedef struct {const TopspinSpriteXbrInput *in;size_t used,limit;int status;} Work;
enum {P0,P1,P2,P3,B,C,D,E,F,G,H,I,F4,I4,H5,I5};
static int stop(Work*w){if(w->in->cancelled&&w->in->cancelled(w->in->user,w->in->generation)){w->status=-1;return 1;}return 0;}
static void* take(Work*w,size_t n){if(!n||w->used>w->limit||n>w->limit-w->used)return NULL;void*p=malloc(n);if(p)w->used+=n;return p;}
static void drop(Work*w,void*p,size_t n){if(p){free(p);w->used-=n;}}
static int image(Work*w,FloatImage*i,uint32_t x,uint32_t y){size_t n=(size_t)x*y*sizeof(Pixel);Pixel*p=take(w,n);if(!p)return 0;*i=(FloatImage){p,x,y,n};return 1;}
static void release(Work*w,FloatImage*i){drop(w,i->p,i->bytes);memset(i,0,sizeof *i);}
static Pixel sample(const FloatImage*i,float x,float y){int a=(int)floorf(x),b=(int)floorf(y);if(a<0)a=0;if(b<0)b=0;if(a>=(int)i->w)a=(int)i->w-1;if(b>=(int)i->h)b=(int)i->h-1;return i->p[(size_t)b*i->w+(unsigned)a];}
static float lo(float a,float b){return a<b?a:b;}static float hi(float a,float b){return a>b?a:b;}
static float clamp(float x,float a,float b){return lo(hi(x,a),b);}
static float df(float a,float b){return fabsf(a-b);}
static float dwd(const float*w,float b0,float b1,float c0,float c1,float c2,float d0,float d1,float d2,float d3,float e1,float e2,float e3,float f2,float f3){
 return w[0]*(df(c1,c2)+df(c1,c0)+df(e2,e1)+df(e2,e3))+w[1]*(df(d2,d3)+df(d0,d1))+w[2]*(df(d1,d3)+df(d0,d2))+w[3]*df(d1,d2)+w[4]*(df(c0,c2)+df(e1,e3))+w[5]*(df(b0,b1)+df(f2,f3));
}
static float hvwd(const float*w,float i1,float i2,float i3,float i4,float e1,float e2,float e3,float e4){return w[3]*(df(i1,i2)+df(i3,i4))+w[0]*(df(i1,e1)+df(i2,e2)+df(i3,e3)+df(i4,e4))+w[2]*(df(i1,e2)+df(i3,e4)+df(e1,i2)+df(e3,i4));}
static float weighted(float a,float b,float c,float d,float q,float half){return a*(-q)+b*(q+half)+c*(q+half)+d*(-q);}
static Pixel filter(const Pixel*p,unsigned phase){
 static const float wp[3][6]={{2,1,-1,4,-1,1},{8,0,0,0,0,0},{1,0,2,3,-2,1}};
 float m[16];for(unsigned k=0;k<16;k++)m[k]=p[k].v[0]*.2126f+p[k].v[1]*.7152f+p[k].v[2]*.0722f+(p[k].v[3]-1.f);
 const float*w=wp[phase];
 float de=dwd(w,m[D],m[B],m[G],m[E],m[C],m[P2],m[H],m[F],m[P1],m[H5],m[I],m[F4],m[I5],m[I4])-dwd(w,m[C],m[F4],m[B],m[F],m[I4],m[P0],m[E],m[I],m[P3],m[D],m[H],m[I5],m[G],m[H5]);
 float hv=hvwd(w,m[F],m[I],m[E],m[H],m[C],m[I5],m[B],m[H5])-hvwd(w,m[E],m[F],m[H],m[I],m[D],m[F4],m[G],m[I4]);
 float t=clamp(fabsf(de)/(.6f+.000001f),0,1);t=t*t*(3-2*t);
 float a=phase==1?.175068f:.129633f,b=phase==1?.0648165f:.087534f;Pixel out;
 for(unsigned c=0;c<4;c++){
  float c1=weighted(p[P2].v[c],p[H].v[c],p[F].v[c],p[P1].v[c],a,.5f);
  float c2=weighted(p[P0].v[c],p[E].v[c],p[I].v[c],p[P3].v[c],a,.5f);
  float c3=weighted(p[D].v[c]+p[G].v[c],p[E].v[c]+p[H].v[c],p[F].v[c]+p[I].v[c],p[F4].v[c]+p[I4].v[c],b,.25f);
  float c4=weighted(p[C].v[c]+p[B].v[c],p[F].v[c]+p[E].v[c],p[I].v[c]+p[H].v[c],p[I5].v[c]+p[H5].v[c],b,.25f);
  float q=(de>=0?c2:c1)*t+(hv>=0?c4:c3)*(1-t);
  float mn=lo(lo(p[E].v[c],p[F].v[c]),lo(p[H].v[c],p[I].v[c]));
  float mx=hi(hi(p[E].v[c],p[F].v[c]),hi(p[H].v[c],p[I].v[c]));out.v[c]=clamp(q,mn,mx);
 }
 out.v[3]=clamp(out.v[3],0,1);for(unsigned c=0;c<3;c++)out.v[c]=clamp(out.v[c],0,out.v[3]);return out;
}
static int grid(Work*w,const FloatImage*src,FloatImage*out,unsigned phase){
 static const int off[16][2]={{-1,-1},{2,-1},{-1,2},{2,2},{0,-1},{1,-1},{-1,0},{0,0},{1,0},{-1,1},{0,1},{1,1},{2,0},{2,1},{0,2},{1,2}};
 for(uint32_t y=0;y<src->h;y++){if(stop(w))return 0;for(uint32_t x=0;x<src->w;x++){
  Pixel p[16];for(unsigned k=0;k<16;k++)p[k]=sample(src,(float)x+off[k][0]-(phase==2?1:0),(float)y+off[k][1]-(phase==2?1:0));out->p[(size_t)y*src->w+x]=filter(p,phase);
 }}return 1;
}
static int interleave(Work*w,const FloatImage*src,const FloatImage*original,FloatImage*out){
 for(uint32_t y=0;y<out->h;y++){if(stop(w))return 0;for(uint32_t x=0;x<out->w;x++){
  Pixel*q=&out->p[(size_t)y*out->w+x];float u=((float)x+.5f)*.5f,v=((float)y+.5f)*.5f;
  if((x&1u)==(y&1u)){*q=sample(x&1u?src:original,u,v);continue;}
  float ax=x&1u?.5f:0,ay=x&1u?0:.5f,bx=x&1u?0:.5f,by=x&1u?.5f:0;Pixel p[16];
  p[P0]=sample(original,u-3*ax,v-3*ay);p[P1]=sample(src,u-3*bx,v-3*by);p[P2]=sample(src,u+3*bx,v+3*by);p[P3]=sample(original,u+3*ax,v+3*ay);
  p[B]=sample(src,u-2*ax-bx,v-2*ay-by);p[C]=sample(original,u-ax-2*bx,v-ay-2*by);p[D]=sample(src,u-2*ax+bx,v-2*ay+by);p[E]=sample(original,u-ax,v-ay);
  p[F]=sample(src,u-bx,v-by);p[G]=sample(original,u-ax+2*bx,v-ay+2*by);p[H]=sample(src,u+bx,v+by);p[I]=sample(original,u+ax,v+ay);
  p[F4]=sample(original,u+ax-2*bx,v+ay-2*by);p[I4]=sample(src,u+2*ax-bx,v+2*ay-by);p[H5]=sample(original,u+ax+2*bx,v+ay+2*by);p[I5]=sample(src,u+2*ax+bx,v+2*ay+by);*q=filter(p,1);
 }}return 1;
}
static int twice(Work*w,const FloatImage*src,FloatImage*out){
 FloatImage a={0},b={0},c={0};int ok=0;
 if(!image(w,&a,src->w,src->h)||!grid(w,src,&a,0)||!image(w,&b,src->w*2,src->h*2)||!interleave(w,&a,src,&b))goto done;
 release(w,&a);if(!image(w,&c,b.w,b.h)||!grid(w,&b,&c,2))goto done;*out=c;memset(&c,0,sizeof c);ok=1;
 done:release(w,&a);release(w,&b);release(w,&c);return ok;
}
static uint8_t byte(float f){return (uint8_t)floorf(clamp(f,0,1)*255.f+.5f);}
int topspin_sprite_xbr_scale(const TopspinSpriteXbrInput*input,TopspinSpriteXbrImage*out){
 if(!input||!out||out->rgba)return 0;
 TopspinSpriteXbrInput in=*input;
 if(!in.rgba||!in.width||!in.height||in.width>TOPSPIN_SPRITE_XBR_MAX_EDGE||in.height>TOPSPIN_SPRITE_XBR_MAX_EDGE||(in.factor!=2&&in.factor!=4)||in.pitch<(size_t)in.width*4||in.work_limit>TOPSPIN_SPRITE_XBR_WORK_LIMIT)return 0;
 if(in.height>1&&in.pitch>(SIZE_MAX-(size_t)in.width*4)/(in.height-1))return 0;
 if((size_t)(in.height-1)*in.pitch+(size_t)in.width*4>in.rgba_bytes)return 0;
 Work w={&in,0,in.work_limit?in.work_limit:TOPSPIN_SPRITE_XBR_WORK_LIMIT,0};FloatImage source={0},scaled={0},four={0};uint8_t*rgba=NULL;size_t bytes=0;int ok=0;
 if(stop(&w)||!image(&w,&source,in.width,in.height))goto done;
 for(uint32_t y=0;y<in.height;y++){if(stop(&w))goto done;for(uint32_t x=0;x<in.width;x++){
  const uint8_t*p=in.rgba+(size_t)y*in.pitch+(size_t)x*4;Pixel*q=&source.p[(size_t)y*in.width+x];float a=p[3]/255.f;q->v[3]=a;for(unsigned c=0;c<3;c++)q->v[c]=(p[c]/255.f)*a;
 }}
 if(!twice(&w,&source,&scaled))goto done;
 release(&w,&source);
 if(in.factor==4){if(!twice(&w,&scaled,&four))goto done;release(&w,&scaled);scaled=four;memset(&four,0,sizeof four);}
 bytes=(size_t)scaled.w*scaled.h*4;rgba=take(&w,bytes);if(!rgba)goto done;
 for(uint32_t y=0;y<scaled.h;y++){if(stop(&w))goto done;for(uint32_t x=0;x<scaled.w;x++){
  Pixel*p=&scaled.p[(size_t)y*scaled.w+x];uint8_t*q=rgba+((size_t)y*scaled.w+x)*4;q[3]=byte(p->v[3]);for(unsigned c=0;c<3;c++)q[c]=p->v[3]>0?byte(p->v[c]/p->v[3]):0;
 }}
 /* One-pixel transparent border color extension avoids a black neighbor in a
  * later straight-alpha bilinear sample. Coverage stays0; hidden source RGB
  * never participates. Pick the highest-coverage visible8-neighbor. */
 for(uint32_t y=0;y<scaled.h;y++){if(stop(&w))goto done;for(uint32_t x=0;x<scaled.w;x++){
  uint8_t*q=rgba+((size_t)y*scaled.w+x)*4;if(q[3])continue;const uint8_t*best=NULL;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int xx=(int)x+dx,yy=(int)y+dy;if(xx<0||yy<0||xx>=(int)scaled.w||yy>=(int)scaled.h)continue;const uint8_t*n=rgba+((size_t)yy*scaled.w+(unsigned)xx)*4;if(n[3]&&(!best||n[3]>best[3]))best=n;}
  if(best)memcpy(q,best,3);
 }}
 if(stop(&w))goto done;
 *out=(TopspinSpriteXbrImage){rgba,bytes,(size_t)scaled.w*4,scaled.w,scaled.h,in.factor,TOPSPIN_SPRITE_XBR_ALGORITHM,in.generation};rgba=NULL;ok=1;
 done:drop(&w,rgba,bytes);release(&w,&source);release(&w,&scaled);release(&w,&four);return ok?1:w.status;
}
void topspin_sprite_xbr_free(TopspinSpriteXbrImage*i){if(i){free(i->rgba);memset(i,0,sizeof *i);}}
