/* Preserve original artwork; extend only sampled court-edge columns. djultra64. */
#include "loading_layout.h"
#include <math.h>
#include <string.h>
_Static_assert(sizeof(TopspinLoadingVertex)==28,"Original FVF144 stride");
int topspin_loading_build_slices(const TopspinLoadingVertex q[4],TopspinLoadingVertex out[12])
{
 const float x[]={0,640,640,0},y[]={0,0,480,480};
 for(unsigned i=0;i<4;i++){
  if(q[i].x!=x[i]||q[i].y!=y[i]||q[i].z!=1||q[i].rhw!=1||q[i].color!=0xffffffffu
     ||!isfinite(q[i].u)||!isfinite(q[i].v)||q[i].u<0||q[i].u>1||q[i].v<0||q[i].v>1)return 0;
 }
 if(q[0].u!=q[3].u||q[1].u!=q[2].u||q[0].v!=q[1].v||q[2].v!=q[3].v
    ||q[0].u>=q[1].u||q[0].v>=q[2].v)return 0;
 const float left[]={0,160,1120},right[]={160,1120,1280};
 for(unsigned slice=0;slice<3;slice++)for(unsigned i=0;i<4;i++){
  TopspinLoadingVertex*v=&out[slice*4+i];*v=q[i];
  v->x=(i==0||i==3)?left[slice]:right[slice];v->y=(i<2)?0:720;
  if(slice==0)v->u=q[0].u;else if(slice==2)v->u=q[1].u;
 }
 return 1;
}

int topspin_loading_build_backed_slices(const TopspinLoadingVertex q[4],uint32_t width,uint32_t height,TopspinLoadingVertex out[12])
{
 if(width==640&&height==480)return topspin_loading_build_slices(q,out);
 if(width!=1024||height!=512)return 0;
 TopspinLoadingVertex content[4];memcpy(content,q,sizeof content);
 int padded=q[1].x==1024&&q[2].x==1024&&q[2].y==512&&q[3].y==512;
 const float left=0.5f/1024.f,top=0.5f/512.f;
 const float right=(padded?1023.5f:639.5f)/1024.f,bottom=(padded?511.5f:479.5f)/512.f;
 /* Original CC1F0 stores0.5/textureWidth and0.5/textureHeight; CC4D0 uses
  * these on UL and1-reciprocal on BR. The filename/accepted720/target
  * guards live in the enclosing hook. Keep arbitrary UVs/other quads intact. */
 for(unsigned i=0;i<4;i++){
  if(q[i].u!=((i==0||i==3)?left:right)||q[i].v!=(i<2?top:bottom))return 0;
  if(padded){
   if(q[i].x!=((i==0||i==3)?0:1024)||q[i].y!=(i<2?0:512))return 0;
   content[i].x=(i==0||i==3)?0:640;content[i].y=i<2?0:480;
   content[i].u=(i==0||i==3)?left:639.5f/1024.f;
   content[i].v=i<2?top:479.5f/512.f;
  }
 }
 return topspin_loading_build_slices(content,out);
}
