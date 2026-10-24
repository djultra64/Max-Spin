/* Owned ordered visual commands; no replay, clocks or guest calls. djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_JOURNAL_H
#define TOPSPIN_VISUAL_SCENE_JOURNAL_H
#include "visual_frame_packet.h"
#define VSJ_MAX_BYTES (64u*1024u*1024u)
#define VSJ_MAX_COMMANDS 8192u
#define VSJ_MAX_USES 8u
#define VSJ_MAX_CHUNKS 8u
#define VSJ_MAX_RESOURCES 1024u
#define VSJ_VERSION 1u
enum {VSJ_PROGRAM_DRAW=1,VSJ_UP_DRAW,VSJ_CLEAR,VSJ_UPLOAD,VSJ_COPY,VSJ_GAMMA,
 VSJ_SWAPCHAIN_CLEAR,VSJ_SWAPCHAIN_BLIT,VSJ_TARGET_SELECT,VSJ_VIEW,VSJ_FINAL_TARGET};
enum {VSJ_INPUT=1,VSJ_TARGET=2};
enum {VSJ_OK=0,VSJ_INVALID,VSJ_OWNER,VSJ_GENERATION,VSJ_BUDGET,VSJ_RESOURCE,
 VSJ_UNKNOWN_WRITER,VSJ_MISSING_WRITER,VSJ_PENDING,VSJ_PAYLOAD,VSJ_POSTWRITE};
enum {VSJ_CHUNK_GEOMETRY=1,VSJ_CHUNK_INDICES,VSJ_CHUNK_PROGRAM,
 VSJ_CHUNK_CONSTANTS,VSJ_CHUNK_MATERIAL,VSJ_CHUNK_NATIVE_ARGS,VSJ_CHUNK_CPU_UPLOAD};
typedef struct VsjRecorder VsjRecorder;
typedef struct VsjFrame VsjFrame;
typedef struct {
 uint64_t frame_serial,simulation_tick,device_epoch;
 VfpGeneration continuity; /* scene/view continuity, NOT frame serial */
} VsjStamp;
typedef struct {
 VfpResource resource;uint64_t guest_epoch;uint32_t access;
} VsjUse;
typedef struct {uint32_t encoding;const void*data;size_t bytes;} VsjChunk;
typedef struct {uint32_t encoding,offset,bytes;} VsjOwnedChunk;
typedef struct {
 uint64_t serial,ticket;uint32_t kind,writer_count,use_count,chunk_count;
 uint32_t use_indices[VSJ_MAX_USES]; /* frame-owned dependency array */
 VsjOwnedChunk chunks[VSJ_MAX_CHUNKS];
 const unsigned char*payload;size_t payload_bytes;
} VsjCommand;
typedef struct {
 VsjUse before,after; /* after==before for input-only dependencies */
 uint32_t retained_index,internal_target; /* UINT32_MAX means no external lease */
} VsjDependency;
typedef struct {
 void*user;int(*owner)(void*);VfpProvider resources;
 size_t max_bytes;uint32_t max_commands,max_live_frames;
} VsjConfig;
/* Configure explicit experiment only; no environment discovery or workers.
 * Provider/allocator context must outlive all detached frames. max_bytes is a
 * shared CPU budget across recorder and every retained previous/current frame.
 * GPU lease budget remains independently enforced by the real provider. */
VsjRecorder*vsj_create(const VsjConfig*);
int vsj_begin(VsjRecorder*,const VsjStamp*);
/* PRE actual writer: retains initial attachment versions ONCE and external
 * texture input versions by exact key. A later texture read of an already
 * produced target is an internal edge, never a postdraw initial snapshot.
 * A single prepared transaction can contain multiple actual rect clears. */
uint64_t vsj_prepare(VsjRecorder*,uint32_t kind,const VsjUse*,uint32_t count);
/* CPU-only exact active-frame ownership preflight. Returns1 only if prepare
 * needs no new retain. No source resolve/copy/queue/locks/guest calls. Provider
 * validate must obey its existing immutable-lease CPU-only contract. A miss is
 * not admission: caller must keep the original outer PRE flush+fresh bindings.
 * Same executor, no intervening callback/event/writer before vsj_prepare. */
int vsj_owned_uses(VsjRecorder*,const VsjUse*,uint32_t count);
/* Exactly at each actual low-level writer. Missing prepare/kind mismatch
 * invalidates the entire current frame. This does not issue GPU/guest work. */
void vsj_writer(VsjRecorder*,uint32_t kind);
/* Original shader-cache probe/cull may return without issuing a writer. */
int vsj_cancel(VsjRecorder*,uint64_t ticket);
/* POST writer: only reads stable host-owned/serialized arguments NOW; copies
 * every supplied chunk before return. For each TARGET in prepare, after supplies
 * the real resulting storage/content version with same dimensions and epoch.
 * INPUT entries supply the identical before entry. No caller arrays retained. */
int vsj_commit(VsjRecorder*,uint64_t ticket,const VsjUse*after,uint32_t count,
 const VsjChunk*,uint32_t chunks);
/* Ordered non-writing state transition; payload must contain complete typed
 * state (target selection / view / final-target). Never substitutes for writer. */
int vsj_state(VsjRecorder*,uint32_t kind,const VsjChunk*,uint32_t chunks);
void vsj_fault(VsjRecorder*,uint32_t reason);
/* Must follow the real completed-target selection. No implicit Present, guest
 * flip, IRQ, event, game callback or fence is emitted by this API. */
int vsj_seal(VsjRecorder*,uint64_t final_target_identity,VsjFrame**out);
uint32_t vsj_status(const VsjRecorder*);
size_t vsj_charged_bytes(const VsjRecorder*);
/* Immutable reader interface. validate checks continuity/retained leases,
 * never compares original-frame serial to the current simulation serial. */
const VsjStamp*vsj_stamp(const VsjFrame*);
uint32_t vsj_command_count(const VsjFrame*);
const VsjCommand*vsj_command(const VsjFrame*,uint32_t index);
const VsjDependency*vsj_dependency(const VsjFrame*,uint32_t index);
int vsj_validate(const VsjFrame*);
int vsj_retained(const VsjFrame*,uint32_t index,VsjUse*,void**borrowed_lease);
/* Caller must finish actual async consumers first; provider's real fence
 * contract applies. No fabricated completion. Owner-thread releases only. */
int vsj_release(VsjFrame**);
int vsj_destroy(VsjRecorder**); /* refuses while detached frames remain */
#endif
