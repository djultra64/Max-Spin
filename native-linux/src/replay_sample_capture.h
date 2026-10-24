/* Bounded original replay-sampler observations. Own work: djultra64. */
#ifndef TOPSPIN_REPLAY_SAMPLE_CAPTURE_H
#define TOPSPIN_REPLAY_SAMPLE_CAPTURE_H
#include <stdint.h>
#define TOPSPIN_REPLAY_SAMPLE_BUDGET (1024u*1024u)
typedef struct TopspinReplayCpuState {
 uint32_t gpr[10]; /* eax,ecx,edx,esp,ebx,esi,edi,ebp,seh_ebp,fp_top */
 double fp_stack[8];
 uint16_t fp_control,fp_cc;int32_t fp_cmp,host_round;
 int32_t direction_flag; /* Reuses the former trailing ABI padding. */
} TopspinReplayCpuState;
void topspin_replay_sample_capture_cpu(const TopspinReplayCpuState *state);
int topspin_replay_sample_capture_init(void);
int topspin_replay_sample_capture_enabled(void);
int topspin_replay_sample_capture_wants(uint64_t elapsed_ns);
/* Guest CPU thread only. Memory is read-only, borrowed for this invocation. */
int topspin_replay_sample_capture_enter(uint64_t elapsed_ns,uint32_t context,
    uint32_t stack,const void *guest_base,uint32_t guest_bytes);
int topspin_replay_sample_capture_enter_apply(uint64_t elapsed_ns,uint32_t context,
    uint32_t stack,const void *guest_base,uint32_t guest_bytes);
void topspin_replay_sample_capture_apply_before(uint32_t context,uint32_t stack);
void topspin_replay_sample_capture_apply_after(void);
void topspin_replay_sample_capture_leave(void);
/* Host thread: no IO while the observed original is in flight. */
int topspin_replay_sample_capture_poll(uint64_t elapsed_ns,int force);
#endif
