/* Compact SOURCE evidence, matched to immutable journal payload. djultra64. */
#include "visual_source_archive.h"
#include "visual_scene_recorder.h"
#include "menu_pc_sha256.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
 VpliPacket packet;
 uint64_t frame,device;
 uint32_t primitive,used,nvertices,nindices,program_start,program_hash,program_revision,constant_revision;
 VfpCamera camera;char geometry[65],indices[65],program[65],constants[65];
} Key;
typedef struct {Key key;VclSnapshot*proof;size_t bytes;} Entry;
struct VsaArchive {
 VsaConfig c;size_t charged;int busy;uint64_t retired;int have_retired;
 Entry entries[];
};
static int enter(VsaArchive*a){if(!a||a->busy)return 0;a->busy=1;if(!a->c.owner(a->c.user)){a->busy=0;return 0;}return 1;}
static int leave(VsaArchive*a,int code){a->busy=0;return code;}
static int tag(const VpliSceneTag*s){return s&&s->version==1&&!s->reserved&&s->scene&&s->view&&s->authority_revision&&s->boundary_ns;}
static int span(const VplSpan*s){return s&&s->identity&&s->epoch&&s->begin<s->end&&!((s->begin|s->end)&3);}
static int snapshot(const VclSnapshot*s,size_t bytes,uint32_t revision){
 if(!s||bytes<offsetof(VclSnapshot,runs)||bytes>sizeof*s||vcl_snapshot_bytes(s)!=bytes||s->revision!=revision)return 0;
 uint32_t end=0;
 for(unsigned i=0;i<s->count;i++){
  const VclRun*r=s->runs+i;
  if(r->reserved||!r->count||r->first>=VCL_LANES||r->count>VCL_LANES-r->first||r->first<end||!r->first_revision||r->first_revision>revision||r->count-1>revision-r->first_revision||!span(&r->words)||r->words.end-r->words.begin!=r->count*4||!tag(&r->source))return 0;
  end=r->first+r->count;
 }
 return 1;
}
static int key(Key*k,const VpliPacket*p,const VfpSource*s){
 if(!p||!s||p->version!=VPLI_PACKET_VERSION||p->method!=0x17fc||p->subchannel>7||!span(&p->packet)||!tag(&p->scene)||s->version!=VFP_VERSION||!s->nvertices||!s->nindices||!s->program_revision||!s->constant_revision||s->program_start>=136||(s->primitive!=1&&s->primitive!=5)||(s->used&0xffff0000u)||!s->vertices||!s->indices||!s->program||!s->constants||s->program_bytes!=136u*16||s->constant_bytes!=192u*16||s->vertex_bytes>VFP_MAX_PACKET_BYTES||s->index_bytes>VFP_MAX_PACKET_BYTES||s->index_bytes!=(uint64_t)s->nindices*2)return 0;
 unsigned per=0;for(unsigned i=0;i<16;i++)per+=(s->used>>i)&1u;if(!per)per=1;
 if(s->vertex_bytes!=(uint64_t)s->nvertices*per*16)return 0;
 memset(k,0,sizeof*k);k->packet=*p;k->frame=s->original_frame;k->primitive=s->primitive;k->used=s->used;k->nvertices=s->nvertices;k->nindices=s->nindices;k->program_start=s->program_start;k->program_hash=s->program_hash;k->program_revision=s->program_revision;k->constant_revision=s->constant_revision;k->camera=s->camera;
 menu_pc_sha256(s->vertices,s->vertex_bytes,k->geometry);menu_pc_sha256(s->indices,s->index_bytes,k->indices);menu_pc_sha256(s->program,s->program_bytes,k->program);menu_pc_sha256(s->constants,s->constant_bytes,k->constants);return 1;
}
VsaArchive*vsa_create(const VsaConfig*c){
 if(!c||!c->owner||!c->owner(c->user)||!c->records||c->records>VSA_MAX_RECORDS||c->bytes>VSA_MAX_BYTES)return NULL;
 size_t n=sizeof(VsaArchive)+(size_t)c->records*sizeof(Entry);if(n>=c->bytes)return NULL;
 VsaArchive*a=calloc(1,n);if(a){a->c=*c;a->charged=n;}return a;
}
int vsa_capture(VsaArchive*a,const VpliPacket*p,const VfpSource*s,const VclSnapshot*proof,size_t bytes,uint64_t device){
 if(!enter(a))return VSA_OWNER;
 Key k;
 if(!device||!key(&k,p,s)||!snapshot(proof,bytes,s->constant_revision)||(a->have_retired&&k.frame<=a->retired))return leave(a,VSA_INVALID);
 k.device=device;
 Entry*slot=NULL;for(unsigned i=0;i<a->c.records;i++)if(!a->entries[i].proof){slot=a->entries+i;break;}
 if(!slot||bytes>a->c.bytes-a->charged)return leave(a,VSA_BUDGET);
 VclSnapshot*owned=malloc(bytes);if(!owned)return leave(a,VSA_BUDGET);
 memcpy(owned,proof,bytes);slot->key=k;slot->proof=owned;slot->bytes=bytes;a->charged+=bytes;return leave(a,VSA_OK);
}
static const void*chunk(const VsjCommand*c,uint32_t encoding,size_t*bytes){
 const void*out=NULL;*bytes=0;
 if(!c||c->chunk_count>VSJ_MAX_CHUNKS)return NULL;
 for(unsigned i=0;i<c->chunk_count;i++){
  const VsjOwnedChunk*k=c->chunks+i;
  if(k->encoding!=encoding)continue;
  if(out||k->offset>c->payload_bytes||k->bytes>c->payload_bytes-k->offset)return NULL;
  out=c->payload+k->offset;*bytes=k->bytes;
 }
 return out;
}
static int command_key(Key*out,const VsjFrame*f,const VsjCommand*c){
 const VsjStamp*stamp=vsj_stamp(f);size_t bytes;const VsrDrawNative*n=chunk(c,VSJ_CHUNK_NATIVE_ARGS,&bytes);
 if(!stamp||!c||c->kind!=VSJ_PROGRAM_DRAW||!n||bytes!=sizeof*n||n->version!=VSR_VERSION||n->kind!=VSJ_PROGRAM_DRAW||n->geometry_encoding!=VSR_GEOMETRY_PROGRAM_PACKED||n->lineage.version!=1||n->lineage.source.simulation_tick!=stamp->simulation_tick||n->lineage.source.scene!=stamp->continuity.scene||n->lineage.source.view!=stamp->continuity.view)return 0;
 VpliPacket p={0};p.version=VPLI_PACKET_VERSION;p.header_word=n->lineage.header_word;p.subchannel=n->lineage.subchannel;p.method=n->lineage.method;p.packet=(VplSpan){n->lineage.allocation_identity,n->lineage.allocation_epoch,n->lineage.wrap_generation,n->lineage.begin,n->lineage.end};
 p.scene=(VpliSceneTag){n->lineage.source.version,n->lineage.source.simulation_tick,n->lineage.source.simulation_time_bits,n->lineage.source.reserved,n->lineage.source.scene,n->lineage.source.view,n->lineage.source.authority_revision,n->lineage.source.boundary_ns};
 VfpSource s={0};s.version=1;s.primitive=n->primitive;s.used=n->used;s.nvertices=n->vertex_count;s.nindices=n->index_count;s.program_start=n->program_start;s.program_hash=n->program_hash;s.program_revision=n->program_revision;s.constant_revision=n->constant_revision;s.original_frame=stamp->frame_serial;s.camera=n->camera;
 s.vertices=chunk(c,VSJ_CHUNK_GEOMETRY,&s.vertex_bytes);s.indices=chunk(c,VSJ_CHUNK_INDICES,&s.index_bytes);s.program=chunk(c,VSJ_CHUNK_PROGRAM,&s.program_bytes);s.constants=chunk(c,VSJ_CHUNK_CONSTANTS,&s.constant_bytes);
 if(!key(out,&p,&s))return 0;out->device=stamp->device_epoch;return out->device!=0;
}
int vsa_lookup(VsaArchive*a,const VsjFrame*f,uint32_t index,const VclSnapshot**out){
 if(out)*out=NULL;
 if(!enter(a))return VSA_OWNER;
 if(!f||!out||!vsj_validate(f))return leave(a,VSA_INVALID);
 const VsjCommand*c=vsj_command(f,index);Key k;if(!command_key(&k,f,c))return leave(a,VSA_MISSING);
 Entry*found=NULL;
 for(unsigned i=0;i<a->c.records;i++){
  Entry*e=a->entries+i;if(!e->proof||memcmp(&e->key,&k,sizeof k))continue;
  if(found)return leave(a,VSA_AMBIGUOUS);found=e;
 }
 if(!found)return leave(a,VSA_MISSING);
 if(!snapshot(found->proof,found->bytes,k.constant_revision))return leave(a,VSA_INVALID);
 *out=found->proof;return leave(a,VSA_OK);
}
int vsa_retire(VsaArchive*a,uint64_t frame){
 if(!enter(a))return VSA_OWNER;
 if(a->have_retired&&frame<a->retired)return leave(a,VSA_INVALID);
 for(unsigned i=0;i<a->c.records;i++){Entry*e=a->entries+i;if(e->proof&&e->key.frame<=frame){a->charged-=e->bytes;free(e->proof);memset(e,0,sizeof*e);}}
 a->retired=frame;a->have_retired=1;return leave(a,VSA_OK);
}
int vsa_destroy(VsaArchive**p){
 if(!p||!*p)return VSA_INVALID;VsaArchive*a=*p;if(!enter(a))return VSA_OWNER;
 for(unsigned i=0;i<a->c.records;i++)free(a->entries[i].proof);free(a);*p=NULL;return VSA_OK;
}
