/* Optional bounded CPU observations; no guest calls or rendering. djultra64. */
#ifndef TOPSPIN_ACTOR_BINDING_CAPTURE_H
#define TOPSPIN_ACTOR_BINDING_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#define VAB_BYTES (256u*1024u)
#define VAB_CHUNKS 160u
#define VAB_BONES 256u
#define VAB_LODS 32u
#define VAB_MESHES 128u
enum {VAB_OFF=0,VAB_OK,VAB_UNBOUND,VAB_INVALID,VAB_RANGE,VAB_CHANGED,VAB_BUDGET,VAB_ORDER,VAB_CLASS};
enum {VAB_OWNER=1,VAB_ACTOR,VAB_DESCRIPTOR,VAB_MANAGER,VAB_COMPONENT,VAB_TRANSFORM,VAB_LOCAL_PALETTE,VAB_WORLD_PALETTE,VAB_LOD_LIST,VAB_MODEL,VAB_MESH_LIST,VAB_MESH,VAB_VTABLE,VAB_ATTACHMENT,VAB_ATTACHMENT_MATRIX,VAB_UPLOAD_INDICES,VAB_UPLOAD_SLOTS,VAB_UPLOAD_MATRIX};
typedef struct {uint64_t scene,view,frame_serial,simulation_tick,device_epoch;} VabStamp;
/* Half-open bytes in one exact emitter allocation incarnation; no wrap. */
typedef struct {uint64_t identity,epoch,wrap_generation;uint32_t begin,end;} VabSpan;
typedef struct {uint32_t role,address,offset,bytes;} VabChunk;
typedef struct {
 unsigned enabled;void *user;
 /* Caller holds the actual guest owner boundary; callbacks never write guest
  * data/registers or invoke title code. Every read must reject truncation. */
 int (*owner)(void*);int (*epoch)(void*,uint64_t*);
 int (*read)(void*,uint32_t,void*,size_t);
} VabReader;
typedef struct {
 uint32_t status,actor,manager,model,chunk_count,used;
 uint32_t upload_count;uint64_t memory_epoch;VabStamp stamp;VabSpan actor_span,model_span,upload_span;
 VabChunk chunks[VAB_CHUNKS];unsigned char data[VAB_BYTES];
} VabRecord;
/* Explicit opt-in only; OFF does not call any callback. No allocation or IO.
 * Entry at original146320, caller must be original F1BD8 or F1CAE. Root hook
 * preserves complete guest CPU/x87/SIMD/stack before/after observations. */
int vab_actor_begin(const VabReader*,uint32_t owner,uint32_t actor,
 uint32_t caller,uint32_t replay_context,const VabStamp*,const VabSpan*,VabRecord*);
/* Caller has already verified actual TSL owner VA+ctor IDs and exact staged
 * source tag. This entry does not invent an unrelated replay receiver. */
int vab_actor_begin_current_owner(const VabReader*,uint32_t owner,uint32_t actor,
 uint32_t caller,const VabStamp*,const VabSpan*,VabRecord*);
/* Nested actual857C0 entry; selected model must occur in captured manager LOD
 * list. Never infer LOD by repeating86810 or any original selection function. */
int vab_model_begin(const VabReader*,uint32_t model,const VabSpan*,VabRecord*);
/* Actual74520 entry from6E49B: captures the exact palette-index and signed
 * register-slot inputs, no guessed mesh state before85450 updates it. */
int vab_upload_begin(const VabReader*,uint32_t caller,uint32_t renderer,
 uint32_t palette,uint32_t indices,uint32_t count,const VabSpan*,VabRecord*);
int vab_upload_end(const VabReader*,const VabSpan*,VabRecord*);
int vab_model_end(const VabReader*,const VabSpan*,VabRecord*);
int vab_actor_end(const VabReader*,const VabSpan*,VabRecord*);
/* Decoder provides its actual original PB command byte span, original stamp
 * and exact serial/revisions. TLS/current actor alone never attests a draw.
 * Matching returns diagnostic ownership only, NOT a bone-to-constant mapping. */
/* GPU-only deferred authority: caller already proved owned CPU source tag and
 * real decoder span. This does not read guest memory or infer a stamp. */
int vab_bind_owned(VabRecord*,const VabStamp*);
int vab_match_draw(const VabRecord*,const VabStamp*,const VabSpan*,
 uint64_t command_serial,uint32_t program_revision,uint32_t constant_revision);
#endif
