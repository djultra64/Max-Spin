/* Last actual PB writer of each VSH constant lane. djultra64. */
#ifndef TOPSPIN_CONSTANT_WORD_LINEAGE_H
#define TOPSPIN_CONSTANT_WORD_LINEAGE_H
#include "visual_pb_bridge.h"
#define VCL_VERSION 1u
#define VCL_ROWS 192u
#define VCL_LANES (VCL_ROWS*4u)
typedef struct {uint64_t generation;VplSpan word;VpliSceneTag source;uint32_t revision;} VclWord;
typedef struct {
 uint32_t version,enabled,load,component,load_known,failed;
 uint64_t generation;uint32_t pending,pending_lane,pending_word,latest_revision;VplSpan pending_span;VpliSceneTag pending_source;void*user;int(*owner)(void*);
 VclWord lanes[VCL_LANES];
} VclLedger;
int vcl_configure(VclLedger*,int enabled,void*,int(*actual_gpu_owner)(void*));
/* Before the real GPU method callback, after VPLI consumed its exact word.
 * Observe EVERY1EA4/0B80..0BFC, including foreign writers/identical values. */
int vcl_method(VclLedger*,uint32_t actual_method,uint32_t actual_word,const VpliPacket*);
/* Actual nv2a_vsh_constant_word POST memcpy/increment, before return. */
int vcl_written(VclLedger*,uint32_t actual_row,uint32_t actual_component,uint32_t actual_word,uint32_t actual_revision);
/* Actual non-PB setter/raw unknown mutation revokes authority immediately. */
void vcl_invalidate(VclLedger*);
void vcl_invalidate_rows(VclLedger*,uint32_t first,uint32_t rows);
/* Same GPU owner and synchronous actual SOURCE only. All four components of
 * every requested row must have their latest writer in this authentic span. */
int vcl_written_by(const VclLedger*,uint32_t first,uint32_t rows,const VplSpan*original_upload,const VpliSceneTag*);
typedef struct {uint32_t first,count,first_revision,reserved;VplSpan words;VpliSceneTag source;} VclRun;
typedef struct {uint32_t version,revision,count,reserved;VclRun runs[VCL_LANES];} VclSnapshot;
/* Actual SOURCE: copy only offsetof(runs)+count*sizeof(run) into owned storage.
 * Budget that exact payload. No ledger or callback pointer escapes. */
int vcl_snapshot(const VclLedger*,uint32_t actual_revision,VclSnapshot*);
size_t vcl_snapshot_bytes(const VclSnapshot*);
int vcl_snapshot_written_by(const VclSnapshot*,uint32_t first,uint32_t rows,const VplSpan*,const VpliSceneTag*);
#endif
