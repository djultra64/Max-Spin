/* Opt-in original compressor source observation. Own work: djultra64. */
#ifndef TOPSPIN_CREATOR_MATERIAL_READ_TRACE_H
#define TOPSPIN_CREATOR_MATERIAL_READ_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TOPSPIN_MATERIAL_READ_MAX_BYTES (512u*512u*4u)
typedef struct {uint32_t addresses[2],width,height;} TopspinMaterialReadRequest;
typedef struct {
 int32_t known[2],selected,gpu_readback;
 uint32_t widths[2],heights[2],allocation_generation_available;
 uint64_t contents_versions[2];
 unsigned char rgba[TOPSPIN_MATERIAL_READ_MAX_BYTES];
} TopspinMaterialReadSnapshot;
/* Combined capture MUST execute all queries/readback in ONE GPU-executor job.
 * It must not invoke Vulkan on the guest caller thread. Buffers are HOST-only;
 * callback may return failure/timeout, with no late writes to caller storage.
 * Return1 only when the entire request completed; output otherwise ignored. */
typedef int (*TopspinMaterialCapture)(const TopspinMaterialReadRequest*,TopspinMaterialReadSnapshot*);
/* Startup-only exact TRACE=1 plus absolute PREFIX; default off, one snapshot. */
int topspin_creator_material_read_configure(TopspinMaterialCapture capture);
/* Proposed root host adapter implemented separately through SDK93. */
int topspin_creator_material_gpu_capture(const TopspinMaterialReadRequest*,TopspinMaterialReadSnapshot*);
#ifdef __cplusplus
}
#endif
#endif
