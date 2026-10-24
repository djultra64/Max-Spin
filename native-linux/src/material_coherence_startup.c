/* Own work djultra64. No renderer call unless explicitly enabled. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "material_coherence_startup.h"
#include "material_source_lock.h"
#include "playercreator_material_tracking_hooks.h"
#include "material_save_session.h"
#include "material_save_transaction.h"
#include "material_save_operation.h"
#include "playercreator_model_checkpoint.h"
#include "resource_coherence_backend.h"
#include <stdlib.h>
#include <string.h>
extern int topspin_creator_material_producer_capture(const McLock*,McSnapshot*,unsigned char*,size_t);
static int configured;
int topspin_creator_material_coherence_configure(const char*save_root){
 const char*on=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");if(!on||strcmp(on,"1"))return 1;
 const char*jobs=getenv("RECOMP_EXECUTOR_INVOKE");if(configured||(jobs&&strcmp(jobs,"1")))return 0;
 if(setenv("RECOMP_EXECUTOR_INVOKE","1",0))return 0;
 if(!mc_save_transaction_configure(save_root)||!d3d8_vk_coherence_configure()||!topspin_creator_material_tracking_configure()||!mc_source_lock_configure(topspin_creator_material_producer_capture)||!mc_save_session_configure()||!mc_save_operation_configure())return 0;
 if(!topspin_creator_model_checkpoint_configure(save_root))return 0;
 configured=1;return 1;
}
