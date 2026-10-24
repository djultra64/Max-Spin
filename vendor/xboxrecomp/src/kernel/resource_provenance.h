/* Original SetRenderTarget PB-span provenance, not a VA heuristic. djultra64. */
#ifndef TOPSPIN_RESOURCE_PROVENANCE_H
#define TOPSPIN_RESOURCE_PROVENANCE_H
#include <stdint.h>
#define RJP_VERSION 1u
#define RJP_QUERY_MIP_LEVEL 1
#define RJP_REGISTER_WRAP_TAIL 1
#define RJP_OBJECTS 128u
#define RJP_EVENTS 512u
#define RJP_WRITES 8192u /* Covers full live Vk registry; exact retirement, no live eviction. */
typedef struct {uint32_t version,reserved;uint64_t serial;} RjpCheckpoint;
typedef struct {
 uint32_t version,resource,parent,header[6],parent_header[5];
 uint32_t resolved_data,width,height,pitch,format;
 /* Guest contiguous VA ring positions, bounds [base,limit). Span includes begin, excludes
  * end, possibly wrapping once. end==limit is a valid exclusive endpoint.
  * Reservation relocation MUST reject upstream. */
 uint32_t ring_base,ring_limit,begin,end;
 uint64_t checkpoint,guest_epoch;
 /* Optional exact tail endpoint from actual original JUMP/reset seam.
  * Zero preserves legacy trusted full-ring span; frontend wrap observer MUST
  * use the real jump DWORD end rather than admitting unused tail padding. */
 uint32_t wrap_tail_end;
} RjpRegister;
typedef struct {uint32_t resource,reserved;} RjpForget;
typedef struct {uint32_t status,reserved;uint64_t guest_id;} RjpRegistered;
typedef struct {
 uint32_t resource,parent,header[6],parent_header[5],resolved_data,width,height,pitch,format;
 uint64_t guest_epoch,allocation,contents;
 /* Exact original-factory 2D mip view, verified before backend admission. */
 uint32_t level;
} RjpQuery;
typedef struct {
 uint64_t guest_id,guest_epoch,allocation,contents,producer_serial,target_serial;
} RjpProof;
/* Configure once before use. owner MUST be executor93 TLS; no renderer APIs
 * invoked on the caller. Events, jobs, query all single-owner/nonreentrant. */
int rjp_configure(int(*owner)(void));
int rjp_enabled(void);
/* Startup-only coverage admission. Backend must reject this once a device
 * exists; no retroactive absence-of-GPU-write claim is permitted. */
int rjp_coverage_begin(void);
void rjp_coverage_unknown(void);
void rjp_gpu_written(uint32_t address,uint32_t bytes,uint64_t actual_kernel_epoch);
/* 0 proves NEVER_GPU for this exact span/lifetime under complete coverage;
 * -1 means UNKNOWN, never an authorization to use stale guest bytes. */
int rjp_cpu_classify(uint32_t address,uint32_t bytes,uint64_t actual_kernel_epoch);
/* Thread-safe failclosed invalidation if any enabled original tracking hook
 * cannot complete its checkpoint/register/forget transaction. No reset. */
void rjp_tracking_fault(void);
/* These exact HOST-buffer work callbacks run via nv2a_executor_invoke93. */
int rjp_checkpoint_job(const void*,uint32_t,void*,uint32_t);
int rjp_register_job(const void*,uint32_t,void*,uint32_t);
int rjp_forget_job(const void*,uint32_t,void*,uint32_t);
/* Called for actual PB SET_SURFACE_COLOR_OFFSET with exact current command PC.
 * Epoch must come from actual allocation span query, not address/high-water. */
int rjp_target(uint32_t pc,uint32_t resolved_color,uint64_t guest_epoch);
/* Actual backend write issued into that physical target, not PGRAPH success.
 * Captured dimensions are guest/logical, independent of raster SSAA density. */
int rjp_write(uint32_t resolved_color,uint64_t guest_epoch,uint64_t allocation,
              uint64_t contents,uint32_t width,uint32_t height,uint32_t pitch,uint32_t format);
/* Native HW color0x28 does not identify guest swizzled6 vs linear18. This
 * actual-writer seam records no guest format until its exact Target binding
 * authenticates one; unknown/late mismatches never authorize a query. */
#define RJP_NATIVE_RGBA_WRITE 1
int rjp_write_native_rgba(uint32_t color,uint64_t epoch,uint64_t allocation,uint64_t contents,
                          uint32_t width,uint32_t height,uint32_t pitch);
/* Must supply current actual backend allocation/content and current guest epoch.
 * No old cached target or earlier ring lap may authorize a lock readback. */
int rjp_query(const RjpQuery*,RjpProof*);
#define RJP_ALLOCATION_RETIRE 1
/* Only actual backend image destruction, never a temporary Surface release. */
int rjp_forget_allocation(uint64_t actual_backend_identity);
#define RJP_QUERY_DIAGNOSTIC 1
/* Optional observation immediately after a rejected owner query. No mutation,
 * guest memory, GPU calls, fabricated identity or change to query admission. */
typedef struct {
 uint32_t version,broken,tracking_fault,retired_resource,retired_parent;
 uint32_t live_objects,related_objects,same_objects,writes_used,allocation_writes,color_writes;
 uint64_t pending_checkpoint,object_id;uint32_t object_live,surface_live,object_same;
 RjpRegister object;
 uint64_t write_serial,write_target,write_epoch,write_allocation,write_contents,write_binding;
 uint32_t write_color,write_width,write_height,write_pitch,write_format;
} RjpQueryDiagnostic;
int rjp_query_diagnostic(const RjpQuery*,RjpQueryDiagnostic*);
#endif
