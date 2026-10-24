/* Proposed immutable CPU draw packet. Own work: djultra64.
 * No GPU resource provider or submission is implemented here. */
#ifndef TOPSPIN_VISUAL_FRAME_PACKET_H
#define TOPSPIN_VISUAL_FRAME_PACKET_H
#include <stddef.h>
#include <stdint.h>
#define VFP_VERSION 1u
#define VFP_MAX_RESOURCES 6u
#define VFP_MAX_PACKET_BYTES (8u*1024u*1024u)
#define VFP_MAX_BUDGET_BYTES (64u*1024u*1024u)
#define VFP_MAX_PACKETS 128u
#define VFP_MATERIAL_MAX_BYTES 8192u
enum {VFP_OK=0,VFP_INVALID,VFP_NO_PROVIDER,VFP_GENERATION,VFP_LIMIT,VFP_ALLOC,VFP_RESOURCE,VFP_BUSY};
enum {VFP_COLOR=0,VFP_DEPTH=1,VFP_TEXTURE0=2,VFP_TEXTURE1=3,VFP_TEXTURE2=4,VFP_TEXTURE3=5};
enum {VFP_CAMERA_BAKED_NV2A=1};
typedef struct {uint64_t scene,view;} VfpGeneration;
typedef struct {
 uint64_t identity,version;uint32_t role,format,width,height,pitch;
} VfpResource;
typedef struct {
 uint32_t encoding,viewport[4],depth_range_bits[2];
 /* Captured fixed-composite transform; full camera/bone constants remain in
  * the exact VSH constant bank. This is not a fabricated standalone camera. */
 float fixed_composite[16];
} VfpCamera;
typedef struct {
 uint32_t version,primitive,used,nvertices,nindices,program_start;
 uint32_t program_hash,program_revision,constant_revision,texture_mask;
 uint64_t original_frame,simulation_tick;VfpGeneration generation;
 const float *vertices;size_t vertex_bytes;
 const uint16_t *indices;size_t index_bytes;
 const uint32_t *program;size_t program_bytes; /* exactly136 four-word instructions */
 const float *constants;size_t constant_bytes; /* exactly192float4 registers */
 VfpCamera camera;
 /* Tagged opaque current capture state, not an unversioned SDK C struct ABI. */
 uint32_t material_encoding;const void *material;size_t material_bytes;
 uint32_t resource_count;const VfpResource *resources;
} VfpSource;
typedef struct {
 void *user;
 /* Every callback required; no provider => reject. Successful retain promises
  * immutable CONTENT version, not only a stable object pointer. On success,
  * retain publishes one non-NULL lease; on failure it publishes no lease and
  * acquires nothing. Release must consume exactly that lease. Provider and
  * its context must outlive packets. No production implementation exists. */
 int (*generation)(void*,VfpGeneration*);
 int (*retain)(void*,const VfpResource*,void **lease);
 int (*validate)(void*,const VfpResource*,void *lease);
 void (*release)(void*,const VfpResource*,void *lease);
} VfpProvider;
typedef struct {
 void *user;void *(*allocate)(void*,size_t);void (*deallocate)(void*,void*);
 size_t max_bytes,live_bytes;unsigned max_packets,live_packets,busy;
 /* Single owner, non-reentrant; all release calls occur on this owner after
  * the actual consumer fence. Budget and allocator context outlive packets. */
} VfpBudget;
typedef struct VfpPacket VfpPacket;
/* Transactional: failure leaves *outNULL, budget/live references unchanged.
 * Source arrays must remain readable/stable during capture; owned after success.
 * Typed arrays require their natural C alignment. Caller synchronizes guest
 * writers during capture; generation checks alone do not cure a data race. */
int vfp_capture(VfpBudget*,const VfpProvider*,const VfpSource*,VfpPacket **out);
const VfpSource *vfp_view(const VfpPacket*);
/* Revalidate before any future submission; changed scene/view/resources reject.
 * This function does NOT submit a draw or prove a real GPU provider exists. */
int vfp_validate(const VfpPacket*);
/* Caller must finish all async consumers first. No implicit fake fence. */
void vfp_release(VfpPacket**);
#endif
