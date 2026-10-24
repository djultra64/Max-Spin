/* Actual emitter-to-decoder byte lineage, optional and bounded. djultra64. */
#ifndef VISUAL_PB_LINEAGE_H
#define VISUAL_PB_LINEAGE_H
#include <stddef.h>
#include <stdint.h>
#define VPL_MAX_RING_BYTES (1u<<20)
#define VPL_TAG_CUTS 128u
/* Actual ring-sized blocks + bounded scene fragmentation + head/wrap slivers. */
#define VPL_INTERVALS (VPL_MAX_RING_BYTES/4096u+VPL_TAG_CUTS+2u)
#define VPL_BYTES 4096u
#define VPL_DEPTH 8u
enum {VPL_OFF=0,VPL_OK,VPL_INVALID,VPL_EPOCH,VPL_ORDER,VPL_OVERWRITE,VPL_BYTES_CHANGED,VPL_BUDGET};
typedef struct {uint64_t identity,epoch,wrap_generation;uint32_t begin,end;} VplSpan;
enum {VPL_MISMATCH_CONSUME=1,VPL_MISMATCH_WRAP};
/* First exact comparison failure only. kind0 means no proof; kind is written
 * last, BEFORE status6. Caller serialization remains mandatory. Consume:
 * expected=owned copied DWORD, observed=actual decoder argument. Wrap:
 * expected=original jump argument, observed=existing copied DWORD. No reread. */
typedef struct {
 uint32_t kind,original_va,canonical,expected,observed;VplSpan span;
} VplMismatch;
typedef struct {
 void*user;int enabled;
 /* Resolve through actual xbox_ContiguousEpoch. canonical must be the real
  * allocation alias, never address masking without an allocation lookup. */
 int(*allocation)(void*,uint32_t,size_t,uint32_t*,uint32_t*,uint32_t*,uint64_t*);
 int(*read)(void*,uint32_t,void*,size_t);
} VplReader;
typedef struct {VplSpan span;uint32_t used,consumed;unsigned char bytes[VPL_BYTES];} VplInterval;
typedef struct {
 VplReader reader;uint32_t initialized,busy,status,base,size,ring_begin,ring_end,producer;
 uint64_t epoch,wrap;uint32_t head,count,depth,interval_limit;
 VplSpan windows[VPL_DEPTH];VplInterval intervals[VPL_INTERVALS];
 VplMismatch first_mismatch;
} VplLedger;
/* Caller serializes producer/decoder operations (same real queue or lock).
 * No IO, allocation, guest mutation, clocks, GPU calls or TLS association. */
/* Storage must be zero initialized; attach is one-shot. Reattach cannot reset
 * wrap while old spans survive. Epoch change faults this cohort permanently. */
int vpl_begin(VplLedger*,const VplReader*,uint32_t ring_begin,uint32_t ring_end,uint32_t actual_put);
int vpl_publish(VplLedger*,uint32_t actual_begin,uint32_t actual_end,VplSpan*);
/* Explicit caller-authorized same-tag append; old publish never merges.
 * Appended bytes are copied once; previously owned bytes are immutable. */
int vpl_append(VplLedger*,uint32_t actual_begin,uint32_t actual_end,VplSpan*);
int vpl_wrap_append(VplLedger*,uint32_t actual_jump_va,uint32_t actual_word,uint32_t actual_reset_put);
/* Only exact original JUMP write plus PUT reset, observed at wrap branch.
 * It is not inferred from a backward cursor or dev+40. */
int vpl_wrap(VplLedger*,uint32_t actual_jump_va,uint32_t actual_word,uint32_t actual_reset_put);
int vpl_window_begin(VplLedger*);
int vpl_window_end(VplLedger*,VplSpan*);
/* Called for EACH real consumed DWORD, including header, args and s_pend.
 * Value is the actual nv2a_pb_read result, not another guest RAM read. */
int vpl_consume(VplLedger*,uint32_t actual_va,uint32_t actual_value,VplSpan*word_source);
int vpl_contains(const VplSpan*parent,const VplSpan*child);
#endif
