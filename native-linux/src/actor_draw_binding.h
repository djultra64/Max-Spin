/* Owned actor/mesh CPU acquisition -> strict decoder draw binding. djultra64. */
#ifndef TOPSPIN_ACTOR_DRAW_BINDING_H
#define TOPSPIN_ACTOR_DRAW_BINDING_H
#include "actor_binding_capture.h"
#include "visual_pb_bridge.h"
#include "visual_frame_packet.h"
#include "constant_word_lineage.h"
#include <pthread.h>
#define VADB_VERSION 1u
#define VADB_RECORDS 64u
/* Fixed budget: four owner slots × sixteen meshes retained until HOST.
 * Exhaustion rejects the owner scope; never evict an unconsumed capture. */
#define VADB_MESH_LIVES 256u
#define VADB_MAX_MAPS 64u
#define VADB_BANK_BYTES (192u*16u)
enum {VADB_OK=1,VADB_OFF,VADB_MISSING,VADB_INVALID,VADB_CHANGED,VADB_BUDGET,VADB_AMBIGUOUS};
typedef struct {uint32_t va,live;uint64_t id;} VadbMeshLife;
typedef struct {
 VabRecord graph;VpliSceneTag source;VabSpan mesh_span;
 uint32_t owner,actor_index,owner_slots,mesh,lod_index;uint64_t mesh_lifetime;unsigned char mesh_header[0x1b4];
 unsigned completed;
} VadbCpuRecord;
typedef struct {
 int enabled;VabReader reader;void*user;
 int(*cpu_owner)(void*);int(*gpu_owner)(void*);
 int(*staged_source)(void*,VpliSceneTag*); /* actual VPLI staged tag, lock/owned */
 int(*current_source)(void*,VpliSceneTag*); /* consumed GPU tag; no RAM */
 int(*owner_ctor)(void*,uint32_t owner,const VpliSceneTag*); /* real TSL VA+ID */
 int(*cursor)(void*,uint32_t actual_put,VabSpan*); /* actual vpli_cursor */
} VadbConfig;
typedef struct {
 VadbConfig config;pthread_mutex_t mutex;int initialized,failed,scope;
 uint64_t next_mesh;VadbMeshLife lives[VADB_MESH_LIVES];
 uint32_t owner,replay,actor,model,mesh,owner_slots;VpliSceneTag source;
 VabRecord scratch;VabSpan mesh_span;uint64_t mesh_lifetime;uint32_t lod_index;
 unsigned actor_chunks,actor_bytes,base_chunks,base_bytes;unsigned char mesh_header[0x1b4];
 unsigned used;VadbCpuRecord records[VADB_RECORDS];
} VadbCollector;
typedef struct {uint16_t palette_index,physical_register;int16_t logical_slot;uint16_t reserved;float rows[12];} VadbMap;
typedef struct {
 uint32_t version,owner,actor_index,owner_slots,actor,model,mesh,lod_index,map_count;
 uint64_t mesh_lifetime,memory_epoch,command_serial;VabStamp stamp;
 VpliSceneTag source;VabSpan actor_span,model_span,mesh_span,draw_span,upload_span;
 uint32_t program_hash,program_revision,constant_revision,primitive,used,nvertices,nindices,program_start;
 char program_sha[65],geometry_sha[65],indices_sha[65],rig_sha[65],material_sha[65];
 uint32_t resource_count;VfpResource resources[6];VadbMap maps[VADB_MAX_MAPS];
 float original_constants[192][4];char writer_snapshot_sha[65];char seal[65];
} VadbBinding;
/* Explicit configure before workers. Storage belongs to caller, default OFF.
 * Hooks observe original calls once under full VabGuestCpu/fenv guard. */
int vadb_configure(VadbCollector*,const VadbConfig*);
uint64_t vadb_mesh_ctor_begin(VadbCollector*,uint32_t va);
int vadb_mesh_ctor_end(VadbCollector*,uint32_t va,uint64_t ticket,uint32_t result);
void vadb_mesh_dtor(VadbCollector*,uint32_t va);
int vadb_owner_begin(VadbCollector*,uint32_t owner);
int vadb_owner_count(VadbCollector*,uint32_t original_return_caller,uint32_t original_count);
int vadb_owner_end(VadbCollector*);
int vadb_actor_begin(VadbCollector*,uint32_t actor,uint32_t caller);
int vadb_model_begin(VadbCollector*,uint32_t model);
int vadb_mesh_begin(VadbCollector*,uint32_t mesh);
int vadb_upload_begin(VadbCollector*,uint32_t caller,uint32_t renderer,uint32_t palette,uint32_t indices,uint32_t count);
int vadb_upload_end(VadbCollector*);
int vadb_mesh_end(VadbCollector*);
int vadb_model_end(VadbCollector*);
int vadb_actor_end(VadbCollector*);
/* Actual decoder next packet begins beyond closed actor scope. This is not a
 * guessed frame counter; safe to reclaim diagnostic input after prior SOURCE. */
int vadb_retire_before(VadbCollector*,const VplSpan* actual_next_packet);
/* GPU path owns no guest reader. Exact real SOURCE and provider epoch/leases,
 * command serial and resources passed by the actual recorder owner only. */
int vadb_bind_draw(VadbCollector*,const VpliPacket*,const VfpSource*,uint64_t actual_device_epoch,uint64_t actual_command_serial,const VfpResource*,unsigned,const void* material,size_t material_bytes,const VclSnapshot* owned_actual_source_writers,VadbBinding*);
int vadb_source_matches(const VadbBinding*,const VfpSource*,const VfpResource*,unsigned,const void* material,size_t material_bytes);
int vadb_pair(const VadbBinding*,const VadbBinding*);
/* Writes owned destination bank only. Endpoints byte-exact, no title updates,
 * guest writes, clock calls, events, flip or device operations. */
int vadb_interpolate(const VadbBinding*,const VadbBinding*,float alpha,float out[192][4]);
#endif
