#include "material_original_probe.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include "material_guest_cpu.h"
extern void sub_001649B0(void);
extern void sub_00162310(void);
extern void sub_00157AD0(void);
static int span(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static uint32_t rd(uint32_t a){uint32_t v=0;if(span(a,4))memcpy(&v,(void*)XBOX_PTR(a),4);return v;}
static void wr(uint32_t a,uint32_t v){memcpy((void*)XBOX_PTR(a),&v,4);}
int mc_original_texture_level_capture(uint32_t texture,uint32_t level,McOriginalProbeCapture capture,void*user,McOriginalProbe*out){
 if(!out||!span(texture,20)||level>=((rd(texture+12)>>16)&15u)||g_esp<0x14000u||!span(g_esp-0x4000,0x4000))return 0;
 /* A resource inside the borrowed call stack cannot remain authoritative
  * while original generated callees execute in that same region. */
 if((uint64_t)texture<g_esp&&(uint64_t)texture+20>g_esp-0x4000u)return 0;
 Cpu saved;cpu_save(&saved);uint32_t base=g_esp-0x4000,sp=base+0x1800,lr=base+0x2000,surface=0;int ok=0;McOriginalProbe value={0};
 unsigned char*backup=malloc(0x4000);if(!backup)return 0;memcpy(backup,(void*)XBOX_PTR(base),0x4000);
 g_esp=sp;wr(sp,0xfec00001);wr(sp+4,texture);wr(sp+8,level);sub_001649B0();surface=g_eax;
 if(g_esp!=sp+12||!span(surface,24)||rd(surface+20)!=texture)goto done;
 memcpy(value.surface_header,(void*)XBOX_PTR(surface),24);
 unsigned format=(value.surface_header[3]>>8)&255u;
 if((value.surface_header[0]&0x70000u)!=0x50000u||(format!=6&&format!=18))goto done;
 wr(lr,0);wr(lr+4,0);g_esp=sp;wr(sp,0xfec00002);wr(sp+4,surface);wr(sp+8,lr);wr(sp+12,0);wr(sp+16,0);sub_00162310();
 if(g_esp!=sp+20)goto done;
 value.pitch=rd(lr);value.bits=rd(lr+4);value.resource=surface;value.level=level;ok=value.pitch&&value.bits;
 if(ok&&capture)ok=capture(user,texture,&value);
 done:
 if(surface&&span(surface,24)){g_esp=sp;wr(sp,0xfec00003);wr(sp+4,surface);sub_00157AD0();if(g_esp!=sp+8)ok=0;}
 memcpy((void*)XBOX_PTR(base),backup,0x4000);free(backup);cpu_restore(&saved);if(ok)*out=value;return ok;
}

int mc_original_texture_level_probe(uint32_t texture,uint32_t level,McOriginalProbe*out){return mc_original_texture_level_capture(texture,level,NULL,NULL,out);}
int mc_original_texture_probe(uint32_t texture,McOriginalProbe*out){return mc_original_texture_level_probe(texture,0,out);}
