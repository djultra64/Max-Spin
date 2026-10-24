/* Synchronous actual-writer transport, default OFF. Own work djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_JOURNAL_BACKEND_H
#define TOPSPIN_VISUAL_SCENE_JOURNAL_BACKEND_H
#include "visual_scene_journal.h"
#include "visual_scene_host_policy.h"
enum {VSJ_DOMAIN_GLOBAL=1,VSJ_DOMAIN_SCENE,VSJ_DOMAIN_HOST_PRESENT};
enum {VSJ_PRE=1,VSJ_POST,VSJ_SOURCE,VSJ_UPLOAD_BYTES,VSJ_GEOMETRY,VSJ_HOST_SOURCE_ONLY};
enum {VSJ_SCENE_ENTER=1,VSJ_SCENE_COMPLETED_TARGET,VSJ_HOST_ENTER,VSJ_HOST_LEAVE};
enum {VSJ_RAW_DRAW=1,VSJ_RAW_INDEXED_DRAW,VSJ_RAW_CLEAR_ATTACHMENTS,
 VSJ_RAW_CLEAR_IMAGE,VSJ_RAW_UPLOAD,VSJ_RAW_BLIT};
typedef struct {
 uint32_t version,kind,phase,domain;const void*arguments;size_t argument_bytes;
 int result;const char*function;
} VsjBackendEvent;
typedef struct {
 uint32_t hash,used,constant_revision;const char*hlsl;const float*constants;
 float zscale,texscale[2];const float*vertices;uint32_t vertex_count;
 const uint16_t*indices;uint32_t index_count;
} VsjProgramArguments;
typedef struct {uint32_t primitive,primitives,stride;const void*vertices;} VsjUpArguments;
#define VSJ_GEOMETRY_FIXED_GPU32 1u
typedef struct {
 const void*vertices;size_t bytes;uint32_t stride,vertex_count,encoding;
} VsjGeometryArguments;
typedef struct {uint32_t count,flags,color,stencil;float depth;const void*rectangles;} VsjClearArguments;
typedef struct {uint64_t image;uint32_t width,height,level,face;const unsigned char*rgba;size_t bytes;} VsjUploadArguments;
typedef struct {uint32_t enabled,width,height;const float*ramp;size_t ramp_bytes;} VsjGammaArguments;
typedef struct {
 void*user;
 /* Explicit install before device/workers. All callbacks required. They run
  * synchronously on the actual caller; never retain argument pointers.
  * PRE may capture resources before original ring/pass/bind. POST/SOURCE copy
  * actual owned inputs while alive. RAW MUST NOT submit/retain/change state:
  * it only classifies and accounts the actual command once. Unknown operation,
  * missing phase/domain/generation/resource/payload MUST return0/fault frame.
  * No success/default provider is supplied by this transport. */
 int(*event)(void*,const VsjBackendEvent*);
 int(*raw)(void*,uint32_t raw_kind,uint32_t domain,const char*function,unsigned line);
 int(*boundary)(void*,uint32_t kind,uint64_t actual_frame,uint32_t completed_color);
 void(*fault)(void*,uint32_t reason);
} VsjBackendHooks;
int vsj_backend_install(const VsjBackendHooks*);
int vsj_backend_enabled(void);
uint32_t vsj_backend_domain(void);
void vsj_backend_event(uint32_t kind,uint32_t phase,const void*args,size_t bytes,int result,const char*function);
void vsj_backend_raw(uint32_t raw_kind,const char*function,unsigned line);
void vsj_backend_boundary(uint32_t kind,uint64_t actual_frame,uint32_t completed_color);
typedef struct {
 uint32_t version,bytes;uint64_t session_id;D3D8SceneHostPolicy policy;
} VsjHostSourceOnlyArguments;
/* Explicit branch receipt, after original final-target copy and before its
 * submission completes. This is not a swapchain writer or display success.
 * The recorder must have a separately configured source-only adopter. */
int vsj_backend_host_source_only(uint64_t session_id,const D3D8SceneHostPolicy*);
/* Actual PB program/constants/camera provenance, not a single-draw snapshot.
 * Callback must copy now; arrays are original decoder-owned and may be reused. */
void vsj_backend_program_source(const VfpSource*);
/* External/filtered GPU producers lacking an owned command codec reject. */
void vsj_backend_untracked(void);
#endif
