/* Continuous delayed-session WSI suppression; explicit default OFF. djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_SOURCE_SESSION_H
#define TOPSPIN_VISUAL_SCENE_SOURCE_SESSION_H
#include <stdint.h>
typedef struct {
 void*user;
 int(*owner)(void*);
 /* Actual continuous delayed stream readiness, not per-frame capture success.
  * Must cover every source/intermediate with the same delay and no old flight. */
 int(*admitted)(void*,uint64_t session,uint64_t actual_device_epoch);
 /* Actual independent/original GPU/WSI consumers drained at a cut, never
  * inferred from a frame number/host time. Synchronous nonblocking host check. */
 int(*drained)(void*,uint64_t session,uint64_t actual_device_epoch);
 /* Host-only notification, never original calls, guest writes or reentry. */
 void(*failure)(void*,uint64_t session,uint32_t reason);
} VssConfig;
enum {VSS_OWNER=1,VSS_EPOCH=2,VSS_ADMISSION=3,VSS_POLICY=4,VSS_RECEIPT=5,VSS_REENTRY=6};
/* Once, before device/workers; no environment-driven installation. */
int d3d8_vk_scene_source_configure(const VssConfig*);
/* Real owner only, frame closed, strict monotonic nonzero session id,
 * actual backend epoch, genuine stream admission and drained boundary. */
int d3d8_vk_scene_source_begin(uint64_t session,uint64_t actual_device_epoch);
/* A failure remains source-only. The sole normal-WSI exit is exact session,
 * actual epoch, frame closed and actual drained cut. No per-frame fallback. */
int d3d8_vk_scene_source_end(uint64_t session,uint64_t actual_device_epoch);
/* Read-only current authority for the typed independent consumer. Failure,
 * epoch/admission mismatch or wrong owner returns0 without enabling fallback.
 * Does not consume a receipt, end a session, submit or infer completion. */
int d3d8_vk_scene_source_validate(uint64_t session,uint64_t actual_device_epoch);
#endif
