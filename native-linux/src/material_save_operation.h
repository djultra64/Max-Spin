/* Complete original Save operation boundary. Own work: djultra64. */
#ifndef TOPSPIN_MATERIAL_SAVE_OPERATION_H
#define TOPSPIN_MATERIAL_SAVE_OPERATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MC_SAVE_OPERATION_CREATOR = 1, MC_SAVE_OPERATION_SHOP = 2 };
/* Startup only, before guest threads: exact coherence env value1 enables this
 * module. It does not configure or grant authority to the transaction backend.
 * Missing startup configure fails enter/phase_begin/finish closed with no IO.
 * Repeated configure calls retain the first configuration. OFF operations are
 * pass-through successes with no IO; active remains false. */
int mc_save_operation_configure(void);
/* One calling-thread scope. Original wrapper must authenticate kind/caller and
 * owner lifetime. This module only bounds/alignment-checks low-RAM owner VA;
 * caller is recorded metadata, never treated as provenance or an allowlist.
 * Nested enter fails and poisons the existing operation without replacing it. */
int mc_save_operation_enter(unsigned kind,uint32_t owner,uint32_t caller);
int mc_save_operation_active(void);
/* Between original phases only: read-only query, never commits or aborts IO.
 * Requires a successful closed phase when enabled; OFF is pass-through. */
int mc_save_operation_can_continue(void);
/* First successful phase_begin opens the transaction exactly once. No new
 * phase starts after rejection; nested/unmatched phases poison the operation.
 * A failed backend begin never grants an end/commit obligation. */
int mc_save_operation_phase_begin(void);
void mc_save_operation_phase_end(int complete);
void mc_save_operation_reject(void);
/* Commit requires >=1 completed phase, no open phase and no rejection. An open
 * transaction otherwise aborts once. Finish stores its result until leave;
 * further finish calls never repeat IO. Finished scopes are not active. */
int mc_save_operation_finish(void);
/* Any unfinished transaction aborts. Always clears TLS for a later independent
 * operation; never commits, resets resource tracking or edits guest CPU state. */
void mc_save_operation_leave(void);
#ifdef __cplusplus
}
#endif
#endif
