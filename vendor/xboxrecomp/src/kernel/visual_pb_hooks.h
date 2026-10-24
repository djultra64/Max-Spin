/* Shared frontend/parser bridge, opt-in only before workers. djultra64. */
#ifndef VISUAL_PB_HOOKS_H
#define VISUAL_PB_HOOKS_H
#include "visual_pb_bridge.h"
#include "visual_frame_packet.h"
typedef int(*VpliProgramSink)(void*,const VpliPacket*,const VfpSource*);
int vpli_hooks_install(VpliBridge*,VpliProgramSink,void*);
/* Actual argument observation after byte-ledger unlock, before original method.
 * NULL packet revokes authority after a failed ledger observation. Synchronous,
 * startup once; never keep the borrowed packet pointer. djultra64. */
typedef void(*VpliMethodObserver)(void*,const VpliPacket*,uint32_t actual_word);
int vpli_method_observer_install(VpliMethodObserver,void*);
int vpli_ack_begin_hook(void);
void vpli_ack_commit_hook(uint32_t);
/* Written once before workers start; never reconfigured concurrently. */
extern VpliBridge *vpli_enabled_bridge;
void vpli_decode_header_impl(uint32_t,uint32_t);
uint32_t vpli_decode_argument_impl(uint32_t,uint32_t);
static inline void vpli_decode_header(uint32_t at,uint32_t word){
 if(vpli_enabled_bridge)vpli_decode_header_impl(at,word);
}
static inline uint32_t vpli_decode_argument(uint32_t at,uint32_t word){
 return vpli_enabled_bridge?vpli_decode_argument_impl(at,word):word;
}
uint32_t vpli_read_header_impl(uint32_t,VpliRawRead);
uint32_t vpli_read_argument_impl(uint32_t,VpliRawRead);
void vpli_program_source(const VfpSource*);
/* Frontend implementation preserves full guest CPU around observations. */
void topspin_pb_lineage_put_store(uint32_t,uint32_t);
void topspin_pb_lineage_wrap(uint32_t,uint32_t,uint32_t);
/* Trusted frontend observation permission only; NEVER actor/scene identity. */
int topspin_pb_observe_scope_enter(void);
void topspin_pb_observe_scope_leave(void);
int topspin_pb_observe_scope_active(void);
/* Full CPU guard plus trusted scope for the original EB620 CPU snapshot sink. */
int topspin_pb_lineage_stage_scene(const VpliSceneTag*);
#endif
