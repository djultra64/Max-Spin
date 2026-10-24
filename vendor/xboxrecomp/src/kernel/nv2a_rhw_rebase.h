/* Conservative proof + uniform homogeneous rebasing prototype. djultra64.
 * Proposed only; not connected to runtime. No guest/SDK/asset writes. */
#ifndef TOPSPIN_PROPOSED_RHW_REBASE_H
#define TOPSPIN_PROPOSED_RHW_REBASE_H
#include <stdint.h>
typedef struct {
 int eligible;
 uint16_t zero_constants[136],one_constants[136];unsigned zero_count,one_count;
 int16_t position_offset[3],position_scale[3];
} RhwRebaseProof;
/* Analyze once per program revision. Supports only a provable passthrough:
 * positionXYZ=input0XYZ*optionalconstant+optionalconstant, W=1/input0W, other outputs independent
 * of input0W. All relative addressing/state writes fail closed. */
int rhw_rebase_analyze(const uint32_t program[136][4],unsigned start,RhwRebaseProof *proof);
/* Caller-owned packed float4 attributes in increasing register order. Attr0
 * must exist. Complete validation precedes any mutation. Return1 rebased,
 * 0 unchanged. Positive uniform power2 factor preserves every vertex's clip
 * vector up to the same factor, including depth/perspective/clipping signs. */
int rhw_rebase_apply(const RhwRebaseProof *proof,const float constants[192][4],
 uint32_t used,float *vertices,unsigned count,float zscale,int *exponent);
#endif
