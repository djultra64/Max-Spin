/* NV2A vertex fog factor contract. Own implementation: djultra64.
 * Behavior reference: xemu glsl/vsh.c; hardware tests nxdk fog_tests.cpp.
 * Preserve unclamped finite factors until fragment interpolation. */
#ifndef XBOXRECOMP_D3D8_FOG_H
#define XBOXRECOMP_D3D8_FOG_H
#include <math.h>
#include <float.h>
static inline float d3d8_nv_fog_factor(float distance,int enabled,unsigned mode,float bias,float multiplier) {
 if(!enabled)return 1.f;
 float f;
 switch(mode) {
 case 0x800:case 0x802:f=bias+exp2f(distance*multiplier*16.f)-1.5f;break;
 case 0x801:case 0x803:f=bias+exp2f(-distance*distance*multiplier*multiplier*32.f)-1.5f;break;
 default:f=bias+distance*multiplier-1.f;break;
 }
 if(mode>=0x802&&mode<=0x804)f=fabsf(f);
 float special=(mode==0x2601||mode==0x804||mode==0x800)?1.f:0.f;
 if(isinf(distance))return special;
 if(isnan(f))return special;
 return fmaxf(-FLT_MAX,fminf(FLT_MAX,f));
}
/* FogData: bias,multiplier,mode,enable. No clamp until fragment stage. */
#define D3D8_NV_FOG_HLSL \
 "float nvFogFactor(float d) {\n" \
 " if (FogData.w == 0) return 1.0;\n" \
 " uint mode = (uint)FogData.z; float f;\n" \
 " if (mode == 2048 || mode == 2050) f = FogData.x + exp2(d * FogData.y * 16.0) - 1.5;\n" \
 " else if (mode == 2049 || mode == 2051) f = FogData.x + exp2(-d*d*FogData.y*FogData.y*32.0) - 1.5;\n" \
 " else f = FogData.x + d*FogData.y - 1.0;\n" \
 " if (mode >= 2050 && mode <= 2052) f = abs(f);\n" \
 " float special = (mode == 9729 || mode == 2052 || mode == 2048) ? 1.0 : 0.0;\n" \
 " if (isinf(d) || isnan(f)) return special;\n" \
 " return clamp(f, -3.402823466e+38, 3.402823466e+38);\n" \
 "}\n"
#endif
