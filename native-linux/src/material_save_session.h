/* Proposed all-sources preflight before original Save descriptor. djultra64. */
#ifndef TOPSPIN_MATERIAL_SAVE_SESSION_H
#define TOPSPIN_MATERIAL_SAVE_SESSION_H
#include "material_coherence.h"
int mc_save_session_configure(void);
void mc_save_session_operation_begin(void);
/* Return1: call original C66A0 ONCE then mc_save_after. Return0: original
 * rejected before ANY of its writes; AL0/stdcall ret4 already published.
 * No Career flags/signatures/progress are changed. DefaultOFF passes through. */
int mc_save_before(void);
void mc_save_after(void);
/* Original7B93C consumes preflight-owned versions during Save, fresh producer proof before consumption.
 * 0=inactive,1=coherent copy,2=proven CPU-only,-1=unexpected source/rebind. */
int mc_save_source_copy(const McLock*,void*,size_t);
int mc_save_active(void);
/* Authentic ScoreName producer return, before original CPU resize/export. */
int mc_save_aux_source(uint32_t resource,uint32_t caller);
void mc_save_export_validate(uint32_t resource,uint32_t target_format);
#endif
