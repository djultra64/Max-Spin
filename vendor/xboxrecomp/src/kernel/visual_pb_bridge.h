/* Optional actual PUT publication / decoder bridge. djultra64. */
#ifndef VISUAL_PB_BRIDGE_H
#define VISUAL_PB_BRIDGE_H
#include "visual_pb_lineage.h"
#include <pthread.h>
#include <stdatomic.h>
#define VPLI_SCENE_TAG_VERSION 1u
typedef struct {
 uint32_t version,simulation_tick,simulation_time_bits,reserved;
 uint64_t scene,view,authority_revision,boundary_ns;
} VpliSceneTag;
typedef struct {
 VplReader memory;void*user;
 int(*decoder_owner)(void*);
 int(*control_read)(void*,uint32_t,uint32_t*);
 void(*fault)(void*,unsigned);
 /* CPU stages an immutable snapshot. GPU binds the actually consumed tag,
  * including invalid/revoked tags, BEFORE original method execution. */
 void(*bind_scene)(void*,const VpliSceneTag*);
 uint64_t(*authority_revision)(void*); /* actual host atomic-only authority */
} VpliConfig;
#define VPLI_PACKET_VERSION 2u
/* Sink copies this fixed owned value synchronously, never retains its pointer. */
typedef struct {
 uint32_t version,header_word,subchannel,method;VplSpan packet;VpliSceneTag scene;
} VpliPacket;
enum {VPLI_FENCE_WAIT=1,VPLI_FENCE_TRAP,VPLI_FENCE_RESTORE};
typedef struct {uint64_t birth,epoch,wrap;uint32_t record,fence,base,size,words[4];} VpliFence;
/* First bridge failure; observed under existing mutex (or its reentrant owner).
 * source_line indexes the pinned bridge C. Read after process stop only. */
typedef struct {
 uint32_t version,reason,source_line,ledger_status,head,count,producer,remaining,next_method,draw_active;
 uint64_t wrap;VpliPacket packet;
} VpliFailure;
typedef struct {
 pthread_mutex_t mutex;VpliConfig config;VplLedger ledger;
 int configured,enabled;atomic_int failed;uint32_t device;
 /* Candidate is PREPUT metadata only, never an admitted byte interval. */
 uint32_t candidate_valid,candidate_device,candidate_begin,candidate_end,candidate_put;
 uint32_t candidate_base,candidate_size;uint64_t candidate_epoch;
 VpliPacket packet;uint32_t remaining,noninc,next_method,draw_active,draw_ready;VplSpan draw_span;
 VpliSceneTag staged_scene,interval_scene[VPL_INTERVALS],consumed_scene,draw_scene;
 uint32_t scene_cuts,interval_cut[VPL_INTERVALS];
 uint64_t fence_birth;VpliFence fences[64];
 VpliFailure first_failure;
 /* Wrapped BEGIN..END cannot fit VpliPacketV2. Reject that whole visual frame,
  * retain exact byte decoding, and recover draw evidence only after realEND. */
 uint32_t draw_discard,draw_wrap_pending;uint64_t discarded_draws;
 VplSpan draw_wrap_source;
 struct {uint32_t version,reserved;VplSpan prefix,transfer;VpliSceneTag scene;} first_wrapped_draw;
} VpliBridge;
/* Static zeroed bridge, startup once. OFF never reads RAM/epoch or locks. */
/* Trusted original1AA290 birth, after its32byte stores before originalPUT.
 * Exact original1AA340 write phases; callback performs original stores once,
 * evenOFF/failure. Decoder read+compare must share this same bridge mutex. */
int vpli_fence_birth(VpliBridge*,uint32_t record,uint32_t fence);
void vpli_fence_patch(VpliBridge*,uint32_t phase,uint32_t record,uint32_t fence,
 void(*original_stores)(void*,uint32_t,uint32_t),void*);
int vpli_configure(VpliBridge*,const VpliConfig*,int enabled);
/* GPU owner locks BEFORE the original initial PUT read; commit unlocks AFTER
 * the original s_run_pos=p assignment. p=0 leaves startup unarmed. */
int vpli_ack_initial_begin(VpliBridge*);
int vpli_ack_initial_commit(VpliBridge*,uint32_t actual_adopted_put);
/* Exact original store seam1AA170@1AA1BC, BEFORE DMA_PUT exposure. Store
 * callback executes once even when capture fails; recording never changes it. */
void vpli_store_put(VpliBridge*,uint32_t target,uint32_t value,
 void(*original_store)(void*,uint32_t,uint32_t),void*);
/* Original1AA4B0 jump+reset branch, before resetting guest PUT. */
int vpli_observe_wrap(VpliBridge*,uint32_t jump_va,uint32_t word,uint32_t reset);
/* Raw actor/model/upload cursor before/after original callbacks: tags are
 * independent of whether those bytes have reached queued PUT yet. */
int vpli_cursor(VpliBridge*,uint32_t actual_guest_put,VplSpan*);
/* Called at actual parser reads, not diagnostic nv2a_pb_read calls. */
typedef uint32_t(*VpliRawRead)(uint32_t actual_va);
/* The original raw read and ledger consume share the writer/producer mutex.
 * On capture failure the original read still executes exactly once. */
int vpli_read_header(VpliBridge*,uint32_t,VpliRawRead,uint32_t*actual_word);
int vpli_read_argument(VpliBridge*,uint32_t,VpliRawRead,uint32_t*actual_word);
int vpli_header(VpliBridge*,uint32_t va,uint32_t actual_word);
int vpli_argument(VpliBridge*,uint32_t va,uint32_t actual_word);
/* Snapshot belongs to the actual currently executing method callback. It is
 * usable only synchronously there; no successful shader/frame stamp implied. */
int vpli_packet(VpliBridge*,VpliPacket*);
/* Before original EB620 rendering, never applied to already queued intervals. */
int vpli_stage_scene(VpliBridge*,const VpliSceneTag*);
/* GPU-owner query: no RAM. Rechecks real atomic authority on every query. */
int vpli_consumed_scene(VpliBridge*,VpliSceneTag*);
/* For actual SOURCE only: complete BEGIN..END byte scope, not merely END. */
int vpli_draw_packet(VpliBridge*,VpliPacket*);
#endif
