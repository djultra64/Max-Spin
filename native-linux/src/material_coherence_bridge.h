/* Proposed single owner-job transaction; djultra64. */
#ifndef TOPSPIN_MATERIAL_COHERENCE_BRIDGE_H
#define TOPSPIN_MATERIAL_COHERENCE_BRIDGE_H
#include "material_coherence.h"
#include "resource_coherence_backend.h"
/* HOST executor callback: must fill actual provenance and owned RGBA. Cannot
 * read guest memory or retain these pointers; all work completes before return.
 * Returns1 EXACT_GPU,2 proven NEVER_GPU,0/error unknown. NEVER_GPU requires
 * full producer coverage from before guest startup, not a failed RT lookup. */
typedef int (*McProducerCapture)(const McLock*,McSnapshot*,unsigned char*,size_t);
/* Called on guest owner AFTER invoke regains guest turn. Must re-read original
 * resource/header/LockRect and validate returned guest writable span afresh.
 * ctx never crosses to executor. Caller preserves original CPU ABI separately. */
typedef int (*McRevalidate)(void*ctx,McLock*current,void**destination,size_t*capacity);
int mc_bridge_configure(McProducerCapture); /* startup, exact env gate, once */
/* 1=committed,2=provenCPU-only (unchanged),0=disabled,-1=rejected. A rejected coherence-required Save must
 * be stopped at original export owner; LockRect return is NOT propagated by
 * original7B910. Do not enable this seam without that caller failure gate. */
typedef struct {McSnapshot proof;void*owned;int kind;} McOwnedSnapshot;
/* First HOST-owned rejected request only. No authority or GPU pointers. */
enum {MCB_ARGUMENT=1,MCB_ALREADY_OWNED,MCB_BUSY,MCB_JOB_ALLOC,
 MCB_INVOKE,MCB_CALLBACK,MCB_LOCK,MCB_BYTES,MCB_COMPACT_ALLOC};
typedef struct {
 uint32_t version,stage,resource,parent,level,width,height;
 uint32_t invoke_observed,proof_observed,lock_compared;
 int32_t invoke_status,callback_result,lock_equal;
 uint64_t epoch,expected_bytes,proof_bytes;
 D3D8VkCoherenceDiagnostic gpu;
} McBridgeDiagnostic;
extern McBridgeDiagnostic topspin_material_bridge_first_failure;
int topspin_material_bridge_first_failure_ready(void);
int mc_bridge_snapshot(const McLock*,McOwnedSnapshot*);
void mc_bridge_snapshot_release(McOwnedSnapshot*);
int mc_bridge_transfer(const McLock*,McRevalidate,void*ctx);
#endif
