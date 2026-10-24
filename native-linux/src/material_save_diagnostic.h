/* Owned first preflight rejection; djultra64. */
#ifndef TOPSPIN_MATERIAL_SAVE_DIAGNOSTIC_H
#define TOPSPIN_MATERIAL_SAVE_DIAGNOSTIC_H
#include <stdint.h>
#include "material_save_sources.h"
enum {MCS_TRACKING=1,MCS_SOURCE_LOCK=2,MCS_EXPORT=3,MCS_MODEL=4,MCS_INVENTORY=5,
 MCS_ALLOC=6,MCS_TEXTURE=7,MCS_FORMAT=8,MCS_BASE_MIP=9,MCS_LEVEL_MIP=10,
 MCS_ORIGINAL_PROBE=11,MCS_PARENT_CHANGED=12,MCS_CAPTURE_LIMIT=13,MCS_SURFACE_VIEW=14,
 MCS_EPOCH=15,MCS_PIXEL_BUDGET=16,MCS_GPU_SNAPSHOT=17,MCS_SWIZZLE=18,
 MCS_TOPOLOGY_CHANGED=19,MCS_FINAL_IDENTITY=20,MCS_STACK=21,MCS_TRANSACTION=22};
typedef struct {
 uint32_t version,stage,model,inventory_count,index,texture,parent,surface,level;
 uint32_t format,width,height,pitch,bits,count;int32_t snapshot_kind;uint64_t epoch;McSaveSourcesDiagnostic inventory;
} McSaveDiagnostic;
extern McSaveDiagnostic topspin_material_save_first_failure;
int topspin_material_save_first_failure_ready(void);
#endif
