/* First existing-observation failure only; own diagnostics djultra64. */
#ifndef TOPSPIN_MATERIAL_TRACKING_DIAGNOSTIC_H
#define TOPSPIN_MATERIAL_TRACKING_DIAGNOSTIC_H
#include <stdint.h>
#include "resource_provenance.h"
enum {MCT_NONE=0,MCT_BEFORE_CURSOR=1,MCT_BEFORE_EPOCH=2,MCT_CHECKPOINT=3,
 MCT_STACK=4,MCT_DEVICE=5,MCT_CURSOR_ORDER=6,MCT_CURSOR_BOUNDS=7,MCT_END_EPOCH=8,
 MCT_END_IDENTITY=9,MCT_RESOURCE=10,MCT_FORMAT=11,MCT_SURFACE=12,MCT_PARENT=13,
 MCT_MIP=14,MCT_SIZE=15,MCT_DIMENSIONS=16,MCT_DATA=17,MCT_SOURCE_EPOCH=18,
 MCT_REGISTER_INVOKE=19,MCT_REGISTER_RESULT=20,MCT_FORGET_INVOKE=21,MCT_FORGET_RESULT=22,MCT_WRAP_RECEIPT=23};
typedef struct {
 uint32_t version,stage,operation,entry,end_esp,device,observed_device;
 uint32_t begin,end,base,size,after_base,after_size;uint64_t epoch,after_epoch;
 int32_t invoke_status,invoke_value;uint32_t checkpoint_valid;
 RjpCheckpoint checkpoint;RjpRegister registration;RjpRegistered registered;
} McTrackingFailure;
/* Static BSS; version1 published only after the whole owned record is written.
 * Read while stopped, or acquire state==2 via host-only API. Never call guest. */
extern McTrackingFailure topspin_material_first_failure;
int topspin_material_first_failure_ready(void);
#endif
