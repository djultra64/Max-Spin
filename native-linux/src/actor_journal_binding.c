/* SOURCE can precede CPU actor end after an original kick. Finalize once owned
 * HOST-sealed packet and closed CPU scopes both exist; never wait on the CPU.
 * No guest reads, queue invocation, GPU submissions or original calls. djultra64. */
#include "actor_journal_binding.h"
#include "visual_scene_recorder.h"
#include <string.h>
static const void*payload(const VsjCommand*c,unsigned encoding,size_t*bytes){const void*out=NULL;*bytes=0;for(unsigned i=0;i<c->chunk_count;i++)if(c->chunks[i].encoding==encoding){if(out||c->chunks[i].offset>c->payload_bytes||c->chunks[i].bytes>c->payload_bytes-c->chunks[i].offset)return NULL;out=c->payload+c->chunks[i].offset;*bytes=c->chunks[i].bytes;}return out;}
int vadb_bind_journal(VadbCollector*c,const VsjFrame*f,uint32_t index,const VclSnapshot*writers,VadbBinding*out){
 if(out)memset(out,0,sizeof*out);
 if(!c||!out||!vsj_validate(f))return VADB_MISSING;
 const VsjStamp*stamp=vsj_stamp(f);const VsjCommand*cmd=vsj_command(f,index);if(!stamp||!cmd||cmd->kind!=VSJ_PROGRAM_DRAW||!cmd->serial||cmd->use_count>6)return VADB_MISSING;
 size_t bytes=0;const VsrDrawNative*n=payload(cmd,VSJ_CHUNK_NATIVE_ARGS,&bytes);
 if(!n||bytes!=sizeof*n||n->version!=VSR_VERSION||n->kind!=VSJ_PROGRAM_DRAW||n->geometry_encoding!=VSR_GEOMETRY_PROGRAM_PACKED||n->lineage.version!=1||n->lineage.source.version!=1||n->lineage.source.scene!=stamp->continuity.scene||n->lineage.source.view!=stamp->continuity.view||n->lineage.source.simulation_tick!=stamp->simulation_tick)return VADB_MISSING;
 VfpSource s;memset(&s,0,sizeof s);s.version=1;s.primitive=n->primitive;s.used=n->used;s.nvertices=n->vertex_count;s.nindices=n->index_count;s.program_start=n->program_start;s.program_hash=n->program_hash;s.program_revision=n->program_revision;s.constant_revision=n->constant_revision;s.original_frame=stamp->frame_serial;s.simulation_tick=stamp->simulation_tick;s.generation=stamp->continuity;s.camera=n->camera;
 s.vertices=payload(cmd,VSJ_CHUNK_GEOMETRY,&s.vertex_bytes);s.indices=payload(cmd,VSJ_CHUNK_INDICES,&s.index_bytes);s.program=payload(cmd,VSJ_CHUNK_PROGRAM,&s.program_bytes);s.constants=payload(cmd,VSJ_CHUNK_CONSTANTS,&s.constant_bytes);s.material=payload(cmd,VSJ_CHUNK_MATERIAL,&s.material_bytes);
 VpliPacket p;memset(&p,0,sizeof p);p.version=VPLI_PACKET_VERSION;p.header_word=n->lineage.header_word;p.subchannel=n->lineage.subchannel;p.method=n->lineage.method;p.packet=(VplSpan){n->lineage.allocation_identity,n->lineage.allocation_epoch,n->lineage.wrap_generation,n->lineage.begin,n->lineage.end};
 _Static_assert(sizeof p.scene==sizeof n->lineage.source,"owned source tag ABI");memcpy(&p.scene,&n->lineage.source,sizeof p.scene);
 VfpResource resources[6];for(unsigned i=0;i<cmd->use_count;i++){const VsjDependency*d=vsj_dependency(f,cmd->use_indices[i]);if(!d||d->after.guest_epoch!=stamp->device_epoch)return VADB_MISSING;resources[i]=d->after.resource;}
 return vadb_bind_draw(c,&p,&s,stamp->device_epoch,cmd->serial,resources,cmd->use_count,s.material,s.material_bytes,writers,out);
}
