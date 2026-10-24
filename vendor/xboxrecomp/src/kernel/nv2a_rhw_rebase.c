/* Own conservative abstract-lane proof, not an emulator implementation.
 * Proposed compatibility work: djultra64. */
#include "nv2a_rhw_rebase.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
enum {ZERO,ONE,OTHER,RHW,RECIP,TAINT,CONSTANT,POSITION};
typedef struct {unsigned char kind,lane;int16_t constant,scale;} Sym;
static Sym sym(unsigned k){return(Sym){(unsigned char)k,0,-1,-1};}
static int tainted(Sym a){return a.kind==RHW||a.kind==RECIP||a.kind==TAINT;}
static Sym combine(Sym a,Sym b){return sym(tainted(a)||tainted(b)?TAINT:OTHER);}
static int zero_constraint(RhwRebaseProof*p,Sym a){
 if(a.kind==ZERO)return 1;
 if(a.kind!=CONSTANT||p->zero_count>=136)return 0;
 p->zero_constants[p->zero_count++]=(uint16_t)a.constant;return 1;
}
static Sym add(RhwRebaseProof*p,Sym a,Sym b){
 if(a.kind==ZERO)return b;
 if(b.kind==ZERO)return a;
 if(a.kind==RECIP&&zero_constraint(p,b))return a;
 if(b.kind==RECIP&&zero_constraint(p,a))return b;
 if(a.kind==POSITION&&a.constant<0&&b.kind==CONSTANT){a.constant=b.constant;return a;}
 if(b.kind==POSITION&&b.constant<0&&a.kind==CONSTANT){b.constant=a.constant;return b;}
 return combine(a,b);
}
static Sym multiply(RhwRebaseProof*p,Sym a,Sym b){
 if(a.kind==ONE)return b;
 if(b.kind==ONE)return a;
 if(a.kind==CONSTANT){Sym t=a;a=b;b=t;}
 if(b.kind==CONSTANT){
  if((a.kind==RECIP||a.kind==RHW)&&p->one_count<136){p->one_constants[p->one_count++]=(uint16_t)b.constant;return a;}
  if(a.kind==POSITION&&a.scale<0&&a.constant<0){a.scale=b.constant;return a;}
 }
 return combine(a,b);
}
static void source(const uint32_t*w,unsigned which,Sym r[13][4],Sym out[4]){
 unsigned neg,sw,mux,reg;
 if(which==0){neg=(w[1]>>8)&1;sw=w[1]&255;mux=(w[2]>>26)&3;reg=w[2]>>28;}
 else if(which==1){neg=(w[2]>>25)&1;sw=(w[2]>>17)&255;mux=(w[2]>>11)&3;reg=(w[2]>>13)&15;}
 else{neg=(w[2]>>10)&1;sw=(w[2]>>2)&255;mux=(w[3]>>28)&3;reg=((w[2]&3)<<2)|(w[3]>>30);}
 for(unsigned i=0;i<4;i++){
  unsigned lane=(sw>>(6-2*i))&3;Sym a=sym(OTHER);
  if(mux==1)a=reg<13?r[reg][lane]:sym(TAINT);
  if(mux==2){unsigned attr=(w[1]>>9)&15;if(attr==0){a=sym(lane==3?RHW:POSITION);a.lane=(unsigned char)lane;}}
  if(mux==3){unsigned c=(w[1]>>13)&255;if(c<192){a=sym(CONSTANT);a.constant=(int16_t)(c*4+lane);}}
  if(neg){if(tainted(a))a=sym(TAINT);else if(a.kind!=ZERO)a=sym(OTHER);}
  out[i]=a;
 }
}
static void masked(Sym dst[4],Sym src[4],unsigned mask){for(unsigned i=0;i<4;i++)if(mask&(8u>>i))dst[i]=src[i];}
int rhw_rebase_analyze(const uint32_t program[136][4],unsigned start,RhwRebaseProof*p){
 if(!p)return 0;
 memset(p,0,sizeof *p);for(unsigned i=0;i<3;i++)p->position_offset[i]=p->position_scale[i]=-1;
 if(!program||start>=136)return 0;
 Sym r[13][4],o[13][4];
 for(unsigned i=0;i<13;i++)for(unsigned j=0;j<4;j++)r[i][j]=o[i][j]=sym(ZERO);
 r[12][3]=sym(ONE);unsigned final=0;
 for(unsigned pc=start;pc<136;pc++){
  const uint32_t*w=program[pc];unsigned mac=(w[1]>>21)&15,ilu=(w[1]>>25)&7;
  unsigned mm=(w[3]>>24)&15,im=(w[3]>>16)&15,om=(w[3]>>12)&15,reg=(w[3]>>20)&15,addr=(w[3]>>3)&255;
  if(mac>13||(!mac&&mm)||(!ilu&&im)||(mac==13&&mm)||(w[3]&2)||(om&&!(w[3]&0x800)))return 0;
  Sym a[4],b[4],c[4],m[4],l[4];source(w,0,r,a);source(w,1,r,b);source(w,2,r,c);
  for(unsigned i=0;i<4;i++){
   m[i]=sym(ZERO);l[i]=sym(ZERO);
   if(mac==1)m[i]=a[i];else if(mac==2)m[i]=multiply(p,a[i],b[i]);else if(mac==3)m[i]=add(p,a[i],c[i]);else if(mac==4)m[i]=add(p,multiply(p,a[i],b[i]),c[i]);
   else if(mac)m[i]=combine(combine(a[i],b[i]),c[i]);
   if(ilu==1)l[i]=c[i];else if(ilu==2)l[i]=c[0].kind==RHW?sym(RECIP):sym(tainted(c[0])?TAINT:OTHER);else if(ilu){l[i]=sym(tainted(c[0])?TAINT:OTHER);if(ilu==7)for(unsigned j=1;j<4;j++)l[i]=combine(l[i],c[j]);}
  }
  /* Dots and ARL depend on all selected lanes, not only the output lane. */
  if(mac==5||mac==6||mac==7||mac==13){Sym v=sym(OTHER);for(unsigned i=0;i<4;i++)v=combine(v,combine(a[i],b[i]));if(mac==13&&tainted(v))return 0;for(unsigned i=0;i<4;i++)m[i]=v;}
  if(mm){if(reg>=13)return 0;masked(r[reg],m,mm);}
  if(im){unsigned ir=mac?1:reg;if(ir>=13)return 0;masked(r[ir],l,im);}
  if(om){if(addr>=13)return 0;if(((w[3]>>2)&1)?!ilu:!mac)return 0;
   Sym*dst=addr?o[addr]:r[12];masked(dst,(w[3]&4)?l:m,om);}
  if(w[3]&1){final=1;break;}
 }
 if(!final||r[12][3].kind!=RECIP)return 0;
 for(unsigned i=0;i<3;i++){if(r[12][i].kind!=POSITION||r[12][i].lane!=i)return 0;p->position_offset[i]=r[12][i].constant;p->position_scale[i]=r[12][i].scale;}
 for(unsigned i=0;i<13;i++)for(unsigned j=0;j<4;j++)if(tainted(o[i][j]))return 0;
 p->eligible=1;return 1;
}
int rhw_rebase_apply(const RhwRebaseProof*p,const float constants[192][4],uint32_t used,float*v,unsigned count,float zscale,int*exponent){
 if(!p||!p->eligible||p->zero_count>136||p->one_count>136||!constants||!v||!(used&1)||used&~65535u||!count||count>65536||!isfinite(zscale)||zscale<0x1p-32f||zscale>1)return 0;
 for(unsigned j=0;j<3;j++)if(p->position_offset[j]<-1||p->position_offset[j]>=768||p->position_scale[j]<-1||p->position_scale[j]>=768)return 0;
 for(unsigned i=0;i<p->zero_count;i++){unsigned c=p->zero_constants[i];if(c>=768||constants[c/4][c%4]!=0)return 0;}
 for(unsigned i=0;i<p->one_count;i++){unsigned c=p->one_constants[i];if(c>=768||constants[c/4][c%4]!=1)return 0;}
 unsigned attrs=0;for(unsigned m=used;m;m&=m-1)attrs++;size_t stride=(size_t)attrs*4;float maximum=0;
 for(unsigned i=0;i<count;i++){
  float rhw=v[i*stride+3];if(!isfinite(rhw)||rhw==0)return 0;float a=fabsf(rhw);if(a>maximum)maximum=a;
 }
 if(maximum<=0x1p126f)return 0;
 int e;frexpf(maximum,&e);int shift=e-64;
 for(unsigned i=0;i<count;i++){
  for(unsigned j=0;j<3;j++){
   int c=p->position_offset[j];float offset=c<0?0:constants[c/4][c%4];int sc=p->position_scale[j];float scale=sc<0?1:constants[sc/4][sc%4];float pos=v[i*stride+j]*scale+offset;
   if(!isfinite(pos)||fabsf(pos)>(j==2?0x1p24f:0x1p16f))return 0;
   if(j==2&&pos!=0&&fabsf(pos)<0x1p-16f)return 0;
  }
 }
 for(unsigned i=0;i<count;i++){float a=fabsf(scalbnf(v[i*stride+3],-shift));if(!isfinite(a)||a<0x1p-64f||a>0x1p64f)return 0;}
 for(unsigned i=0;i<count;i++)v[i*stride+3]=scalbnf(v[i*stride+3],-shift);
 if(exponent)*exponent=shift;
 return 1;
}
