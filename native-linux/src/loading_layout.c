/* Stretch the original image, excluding unused POT texels. djultra64. */
#include "loading_layout.h"
#include <string.h>
#include <math.h>
_Static_assert(sizeof(TopspinLoadingVertex)==28,"Original FVF144 stride");
int topspin_loading_build_stretched(const TopspinLoadingVertex q[4],uint32_t width,uint32_t height,TopspinLoadingVertex out[4])
{
 if(!q||!out||!((width==640&&height==480)||(width==1024&&height==512)))return 0;
 int padded=width==1024&&q[1].x==1024&&q[2].x==1024&&q[2].y==512&&q[3].y==512;
 float authored_w=padded?1024.f:640.f,authored_h=padded?512.f:480.f;
 float left=.5f/width,top=.5f/height,right=(authored_w-.5f)/width,bottom=(authored_h-.5f)/height;
 for(unsigned i=0;i<4;i++){
  if(q[i].x!=((i==0||i==3)?0:authored_w)||q[i].y!=(i<2?0:authored_h)
     ||q[i].z!=1||q[i].rhw!=1||q[i].color!=0xffffffffu
     /* The original1-halftexel subtraction can round one ULP differently
      * from division when an earlier loader used another FP rounding mode. */
     ||!isfinite(q[i].u)||!isfinite(q[i].v)
     ||fabsf(q[i].u-((i==0||i==3)?left:right))>0.0000001f
     ||fabsf(q[i].v-(i<2?top:bottom))>0.0000001f)return 0;
 }
 TopspinLoadingVertex stretched[4];memcpy(stretched,q,sizeof stretched);
 for(unsigned i=0;i<4;i++){
  stretched[i].x=(i==0||i==3)?0:1280;stretched[i].y=i<2?0:720;
  stretched[i].u=(i==0||i==3)?left:639.5f/width;stretched[i].v=i<2?top:479.5f/height;
 }
 memcpy(out,stretched,sizeof stretched);return 1;
}
