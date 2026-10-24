/* Executor-only, atomically validated logical RGBA source. djultra64. */
#ifndef TOPSPIN_RESOURCE_COHERENCE_BACKEND_H
#define TOPSPIN_RESOURCE_COHERENCE_BACKEND_H
#include "resource_provenance.h"
typedef struct {RjpProof source;uint32_t get,put,width,height,pitch;} D3D8VkCoherenceProof;
/* All arguments HOST owned; input header captured by original SetRT/Lock hooks.
 * Actual kernel epoch, backend ID/version and drained PB are checked before/
 * after fenced readback. Failure preserves both caller buffer and proof.
 * Must run INSIDE one93 job, never from guest CPU thread. */
/* Configure exact tracking BEFORE device creation. One exporter initializer;
 * no late coverage and no device recreation are modeled. */
int d3d8_vk_coherence_configure(void);
/* 0 NEVER_GPU under complete lifetime/span coverage; output unchanged.
 * 1 exact validated GPU copy; -1 UNKNOWN/reject. No query miss is CPU-only. */
int d3d8_vk_coherence_source(const RjpQuery*,void*,uint32_t,D3D8VkCoherenceProof*);
int d3d8_vk_coherence_read(const RjpQuery*,void*rgba,uint32_t bytes,D3D8VkCoherenceProof*);
/* Private first rejection. Scope points ONLY at current owner-job output;
 * caller copies it to HOST BSS only after executor DONE. djultra64. */
enum {
 MCD_PRODUCER_ARGS=1,MCD_PRODUCER_CAPACITY,MCD_PRODUCER_MIP,MCD_PRODUCER_BACKEND,MCD_PRODUCER_PROOF,
 MCD_SOURCE_ARGS=10,MCD_SOURCE_OWNER,MCD_SOURCE_TRACKING,MCD_SOURCE_DEVICE,MCD_SOURCE_SHAPE,MCD_SOURCE_EPOCH,
 MCD_READ_ARGS=20,MCD_READ_OWNER,MCD_READ_TRACKING,MCD_READ_DEVICE,MCD_READ_SHAPE,MCD_DRAIN_BEFORE,MCD_EPOCH_BEFORE,
 MCD_RT_MISSING,MCD_RT_DEPTH,MCD_RT_WIDTH,MCD_RT_HEIGHT,MCD_RT_PITCH,MCD_RT_FORMAT_HIGH,MCD_RT_FORMAT_LOW,
 MCD_REGISTRY_MISSING,MCD_REGISTRY_CONTENT,MCD_RJP_QUERY,MCD_STAGING,MCD_READBACK,
 MCD_POST_RT,MCD_POST_REGISTRY,MCD_POST_ALLOCATION,MCD_POST_CONTENT,MCD_POST_EPOCH_LOOKUP,MCD_POST_EPOCH,
 MCD_POST_BASE,MCD_POST_SIZE,MCD_POST_DRAIN,MCD_POST_GET,MCD_POST_PUT,MCD_POST_RJP,MCD_POST_GUEST,MCD_POST_SERIAL
};
typedef struct {
 uint32_t version,stage,bytes,observed;int32_t classify,backend_result;
 RjpQuery query;RjpProof source;RjpQueryDiagnostic rjp;
 uint32_t base,size,after_base,after_size,get,put,after_get,after_put;
 uint64_t epoch,after_epoch;
} D3D8VkCoherenceDiagnostic;
/* Trusted internal HOST scope. Only effective after startup explicit diagnostic
 * flag; NULL closes scope. No owner/guest/GPU query or publication is performed. */
void d3d8_vk_coherence_diagnostic_scope(D3D8VkCoherenceDiagnostic*);
void d3d8_vk_coherence_diagnostic_reject(uint32_t stage,const RjpQuery*);
void d3d8_vk_coherence_diagnostic_result(int result);
#endif
