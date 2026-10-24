/* Real-writer -> owned journal adapter. Own work djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_RECORDER_H
#define TOPSPIN_VISUAL_SCENE_RECORDER_H
#include "visual_scene_journal_backend.h"
#include "visual_scene_provider.h"
#include "visual_scene_host_policy.h"
#define VSR_VERSION 1u
#define VSR_MAX_MATERIAL 8192u
#define VSR_GEOMETRY_FIXED_GPU32 1u
#define VSR_GEOMETRY_PROGRAM_PACKED 2u
#define VSR_MAX_UPLOAD (16u*1024u*1024u)
typedef struct VsrRecorder VsrRecorder;
typedef struct {
 uint32_t version,simulation_tick,simulation_time_bits,reserved;
 uint64_t scene,view,authority_revision,boundary_ns;
} VsrSourceTag;
/* Optional authentic decoder BEGIN..END evidence. version0 means UNBOUND;
 * its presence alone never authorizes an actor match or interpolation. */
typedef struct {
 uint32_t version,header_word,subchannel,method;
 uint64_t allocation_identity,allocation_epoch,wrap_generation;
 uint32_t begin,end;VsrSourceTag source;
} VsrPacketLineage;
typedef struct {
 uint32_t version,kind,primitive,primitive_count,used,vertex_count,index_count,vertex_stride;
 uint32_t program_hash,program_revision,constant_revision,program_start,target_active;
 uint32_t geometry_encoding,emitted_vertex_count,emitted_vertex_stride;
 float zscale,texscale[2];D3D8SceneViewState view;VfpCamera camera;VsrPacketLineage lineage;
} VsrDrawNative;
typedef struct {
 uint32_t version,flags,color,stencil,count,target_active;float depth;D3D8SceneViewState view;
 /* count original signed int32[4] rectangles follow, owned in same chunk. */
} VsrClearNative;
typedef struct {uint32_t version,width,height,level,face,cube,bytes,reserved;} VsrUploadNative;
/* Ordered setters retain their typed scalar payload; no pointer-bearing target
 * descriptor is serialized. Resources are actual registry identities. */
typedef struct {
 uint32_t version,opcode,result,target_active,scalar_bytes,resource_count;
 D3D8SceneViewState view;VfpResource resources[6];unsigned char scalar[16];
} VsrStateNative;
enum {VSR_STATE_TARGET=1,VSR_STATE_SCISSOR,VSR_STATE_POLYGON,
 VSR_STATE_BLEND,VSR_STATE_PLACEMENT,VSR_STATE_POINT};
typedef struct {
 uint32_t version,gamma_applied,raw_copies,raw_gamma,raw_swap_clears,raw_swap_blits;
 VsrSourceTag source;D3D8SceneHostPolicy policy;
} VsrHostPolicy;
typedef struct {
 uint32_t version,reserved;VsrSourceTag source;VsjHostSourceOnlyArguments display;
} VsrSourceOnlyPolicy;
typedef int(*VsrSourceOnlyPublish)(void*,VsjFrame**,const VsrSourceOnlyPolicy*);
/* Configure once before the first attempted GPU scene. A missing callback
 * rejects source-only receipts; original WSI receipt contract is unchanged. */
int vsr_source_only_configure(VsrRecorder*,VsrSourceOnlyPublish,void*);
typedef struct {
 void*user;int(*owner)(void*);
 /* Actual currently consumed PB packet's owned source tag, NOT latest queued
  * TSL/GPU publication job. No guest reads, queues, submit or side effects.
  * Invoked again for every observed event/writer/boundary; all source tags in
  * one frame must agree on original tick and ctor continuity. Actual PB frame
  * serial comes from this argument, never used as a lifetime identity. */
 int(*current)(void*,uint64_t actual_frame,VsjStamp*);
 /* Same actual consumed interval/BEGIN..END tag; full owned value required.
  * Never return mutable latest/staged metadata from a publication job. */
 int(*source_tag)(void*,VsrSourceTag*);
 int(*provider)(void*,VfpProvider*,uint64_t*actual_device_epoch);
 int(*prepare)(void*);
 int(*bindings)(void*,unsigned flags,VfpResource*,unsigned capacity,unsigned*count);
 int(*upload)(void*,uint64_t native_image,VfpResource*);
 int(*material)(void*,void*,uint32_t capacity,uint32_t*bytes);
 int(*view)(void*,D3D8SceneViewState*);
 int(*completed)(void*,uint32_t actual_guest_color,VfpResource*);
 int(*host_policy)(void*,D3D8SceneHostPolicy*);
 /* Success takes frame ownership and MUST set *frame=NULL. Failure takes
  * nothing. Policy argument is temporary; copy before returning. */
 int(*publish)(void*,VsjFrame**,const VsrHostPolicy*);
 /* Revoke all extra-output tickets on missing/unknown authority. Does not stop
  * original rendering; must not submit or invoke original title callbacks. */
 void(*invalidate)(void*,uint32_t reason);
 size_t max_bytes,upload_budget;
} VsrConfig;
typedef struct {uint64_t begun,published,rejected,raw_scene,raw_host,raw_global;} VsrStats;
/* Explicit opt-in; installs one backend transport. No game/device/worker/job
 * starts. Real provider is acquired lazily on the actual executor. */
VsrRecorder*vsr_create(const VsrConfig*);
int vsr_stats(const VsrRecorder*,VsrStats*);
/* Synchronous optional VPLI sink, BEFORE actual VSJ_SOURCE. Copies all fields;
 * only a matching real pending PROGRAM POST can acquire packet evidence. */
int vsr_program_lineage(VsrRecorder*,const VsrPacketLineage*,const VfpSource*);
enum {VSR_LINEAGE_REJECT=0,VSR_LINEAGE_ACCEPT=1,VSR_LINEAGE_DISCARDED=2};
/* Compatible new sink status:2 means valid SOURCE belongs to already rejected
 * attempted frame; it admits NO lineage/frame and must not create a new fault.
 * Old bool API above remains unchanged. Caller still validates actual packet
 * and currently consumed source authority before using this sink result. */
int vsr_program_lineage_status(VsrRecorder*,const VsrPacketLineage*,const VfpSource*);
/* Transport is process lifetime; no destroy until an uninstall handshake exists.
 * All detached frames remain under the caller's actual consumer-fence contract.
 * No scene/raw/source API can call Present, update, a guest event, IRQ or flip. */
#endif
