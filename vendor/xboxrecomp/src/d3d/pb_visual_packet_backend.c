#include "pb_visual_packet_backend.h"
#include "d3d8_vk_snapshot.h"
#include "nv2a_executor_job.h"
#include <string.h>
static int(*generation_fn)(void*,VfpGeneration*);
static void*generation_user;
static int owner(void*u){(void)u;return nv2a_executor_is_owner();}
static int generation(void*u,VfpGeneration*g){(void)u;return generation_fn&&generation_fn(generation_user,g);}
static uint64_t serial(void*u){(void)u;return d3d8_vk_snapshot_draw_serial();}
static int capture(void*u,VfpBudget*b,const VfpSource*s,VfpPacket**p){(void)u;return d3d8_vk_snapshot_capture(b,s,p);}
static int release(void*u,VfpPacket**p){(void)u;return d3d8_vk_snapshot_release(p);}
int pb_visual_install_backend(int(*fn)(void*,VfpGeneration*),void*user){
 if(!fn||generation_fn)return 0;
 if(!d3d8_vk_snapshot_executor_configure())return 0;
 PbVisualBackend backend={NULL,owner,generation,serial,capture,release};
 if(!pb_visual_configure(&backend))return 0;
 generation_fn=fn;generation_user=user;return 1;
}
int pb_visual_record_backend(uint64_t ticket,const VfpSource*s,float su,float sv,int rebase){
 struct {uint32_t version,bytes;float texscale[2];int32_t rhw_rebase;unsigned char state[7000];} material;
 if(!ticket||!s||!nv2a_executor_is_owner())return 0;
 memset(&material,0,sizeof material);uint32_t bytes=0;
 if(!d3d8_vk_snapshot_draw_state(material.state,sizeof material.state,&bytes))return 0;
 material.version=0x50564231u;material.bytes=bytes;material.texscale[0]=su;material.texscale[1]=sv;material.rhw_rebase=rebase;
 VfpSource source=*s;source.material_encoding=1u;source.material=&material;
 source.material_bytes=sizeof material-sizeof material.state+bytes;
 return pb_visual_after(ticket,&source);
}
