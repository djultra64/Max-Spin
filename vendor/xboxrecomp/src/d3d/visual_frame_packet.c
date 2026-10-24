/* Own work: djultra64. Proposed CPU ownership boundary; no GPU submission. */
#include "visual_frame_packet.h"
#include <string.h>
#include <limits.h>
struct VfpPacket {
 VfpSource source; VfpProvider provider; VfpBudget *budget;
 size_t charged; unsigned retained; void *leases[VFP_MAX_RESOURCES];
};
static int same_generation(VfpGeneration a,VfpGeneration b){return a.scene==b.scene&&a.view==b.view;}
static int generation_ok(const VfpPacket*p){VfpGeneration g;return p->provider.generation(p->provider.user,&g)&&same_generation(g,p->source.generation);}
static int add(size_t *n,size_t bytes,size_t *offset){
 size_t aligned;if(*n>SIZE_MAX-15)return 0;aligned=(*n+15)&~(size_t)15;
 if(bytes>VFP_MAX_PACKET_BYTES||aligned>VFP_MAX_PACKET_BYTES-bytes)return 0;
 *offset=aligned;*n=aligned+bytes;return 1;
}
static int source_ok(const VfpSource*s){
 unsigned attributes=0,roles=0;uint32_t used;
 if(!s||s->version!=VFP_VERSION||s->primitive!=5||!s->used||(s->used&~0xffffu))return VFP_INVALID;
 if(s->nvertices>32768||s->nindices>98304)return VFP_LIMIT;
 if(!s->nvertices||!s->nindices||s->nindices%3)return VFP_INVALID;
 for(used=s->used;used;used>>=1)attributes+=used&1;
 if(!s->vertices||!s->indices||!s->program||!s->constants||!s->material||!s->resources)return VFP_INVALID;
 if((uintptr_t)s->vertices%_Alignof(float)||(uintptr_t)s->indices%_Alignof(uint16_t)||(uintptr_t)s->program%_Alignof(uint32_t)||(uintptr_t)s->constants%_Alignof(float)||(uintptr_t)s->resources%_Alignof(VfpResource))return VFP_INVALID;
 if(s->vertex_bytes!=(size_t)s->nvertices*attributes*16||s->index_bytes!=(size_t)s->nindices*2||s->program_bytes!=136*16||s->constant_bytes!=192*16)return VFP_INVALID;
 if(s->program_start>=136||s->camera.encoding!=VFP_CAMERA_BAKED_NV2A||s->material_encoding!=1||!s->material_bytes||s->material_bytes>VFP_MATERIAL_MAX_BYTES)return VFP_INVALID;
 if(!s->camera.viewport[2]||!s->camera.viewport[3]||s->camera.viewport[0]>16384||s->camera.viewport[1]>16384||s->camera.viewport[2]>16384-s->camera.viewport[0]||s->camera.viewport[3]>16384-s->camera.viewport[1])return VFP_INVALID;
 if(!s->resource_count||s->resource_count>VFP_MAX_RESOURCES||(s->texture_mask&~15u))return VFP_INVALID;
 for(uint32_t i=0;i<s->resource_count;i++){
  const VfpResource*r=&s->resources[i];
  if(r->role>=VFP_MAX_RESOURCES||(roles&(1u<<r->role))||!r->identity||!r->version||!r->width||!r->height||r->width>16384||r->height>16384||(r->role<=VFP_DEPTH&&!r->pitch))return VFP_INVALID;
  roles|=1u<<r->role;
 }
 if(!(roles&1)||((roles>>2)!=s->texture_mask))return VFP_INVALID;
 for(uint32_t i=0;i<s->nindices;i++)if(s->indices[i]>=s->nvertices)return VFP_INVALID;
 return VFP_OK;
}
int vfp_capture(VfpBudget*b,const VfpProvider*provider,const VfpSource*s,VfpPacket**out){
 size_t bytes=sizeof(VfpPacket),offsets[6];VfpPacket*p=NULL;int status;
 if(!out)return VFP_INVALID;
 *out=NULL;
 if(!provider||!provider->generation||!provider->retain||!provider->validate||!provider->release)return VFP_NO_PROVIDER;
 if(!b||!b->allocate||!b->deallocate)return VFP_INVALID;
 if(b->busy)return VFP_BUSY;
 status=source_ok(s);if(status!=VFP_OK)return status;
 if(!b->max_bytes||b->max_bytes>VFP_MAX_BUDGET_BYTES||!b->max_packets||b->max_packets>VFP_MAX_PACKETS||b->live_packets>=b->max_packets||b->live_bytes>b->max_bytes)return VFP_LIMIT;
 if(!add(&bytes,s->vertex_bytes,&offsets[0])||!add(&bytes,s->index_bytes,&offsets[1])||!add(&bytes,s->program_bytes,&offsets[2])||!add(&bytes,s->constant_bytes,&offsets[3])||!add(&bytes,s->material_bytes,&offsets[4])||!add(&bytes,s->resource_count*sizeof(VfpResource),&offsets[5])||bytes>b->max_bytes-b->live_bytes)return VFP_LIMIT;
 b->busy=1;
 {VfpGeneration g;if(!provider->generation(provider->user,&g)||!same_generation(g,s->generation)){status=VFP_GENERATION;goto done;}}
 p=b->allocate(b->user,bytes);if(!p){status=VFP_ALLOC;goto done;}
 memset(p,0,sizeof *p);p->source=*s;p->provider=*provider;p->budget=b;p->charged=bytes;
 unsigned char*base=(unsigned char*)p;
 memcpy(base+offsets[0],s->vertices,s->vertex_bytes);p->source.vertices=(const float*)(base+offsets[0]);
 memcpy(base+offsets[1],s->indices,s->index_bytes);p->source.indices=(const uint16_t*)(base+offsets[1]);
 memcpy(base+offsets[2],s->program,s->program_bytes);p->source.program=(const uint32_t*)(base+offsets[2]);
 memcpy(base+offsets[3],s->constants,s->constant_bytes);p->source.constants=(const float*)(base+offsets[3]);
 memcpy(base+offsets[4],s->material,s->material_bytes);p->source.material=base+offsets[4];
 memcpy(base+offsets[5],s->resources,s->resource_count*sizeof(VfpResource));p->source.resources=(const VfpResource*)(base+offsets[5]);
 for(unsigned i=0;i<p->source.resource_count;i++){
  if(!p->provider.retain(p->provider.user,&p->source.resources[i],&p->leases[i])||!p->leases[i]){status=VFP_RESOURCE;goto rollback;}
  p->retained++;
 }
 if(!generation_ok(p)){status=VFP_GENERATION;goto rollback;}
 for(unsigned i=0;i<p->retained;i++)if(!p->provider.validate(p->provider.user,&p->source.resources[i],p->leases[i])){status=VFP_RESOURCE;goto rollback;}
 if(!generation_ok(p)){status=VFP_GENERATION;goto rollback;}
 b->live_bytes+=bytes;b->live_packets++;*out=p;status=VFP_OK;goto done;
rollback:
 while(p->retained){unsigned i=--p->retained;p->provider.release(p->provider.user,&p->source.resources[i],p->leases[i]);}
 b->deallocate(b->user,p);
done:
 b->busy=0;return status;
}
const VfpSource*vfp_view(const VfpPacket*p){return p?&p->source:NULL;}
int vfp_validate(const VfpPacket*p){
 if(!p)return VFP_INVALID;
 if(!generation_ok(p))return VFP_GENERATION;
 for(unsigned i=0;i<p->retained;i++)if(!p->provider.validate(p->provider.user,&p->source.resources[i],p->leases[i]))return VFP_RESOURCE;
 return generation_ok(p)?VFP_OK:VFP_GENERATION;
}
void vfp_release(VfpPacket**packet){
 if(!packet||!*packet)return;
 VfpPacket*p=*packet;VfpBudget*b=p->budget;*packet=NULL;
 /* The single-owner contract excludes concurrent/reentrant release. */
 b->busy=1;
 while(p->retained){unsigned i=--p->retained;p->provider.release(p->provider.user,&p->source.resources[i],p->leases[i]);}
 b->live_bytes-=p->charged;b->live_packets--;b->deallocate(b->user,p);b->busy=0;
}
