/* Proposed original compressor source coherence. Own work: djultra64. */
#ifndef TOPSPIN_MATERIAL_COHERENCE_H
#define TOPSPIN_MATERIAL_COHERENCE_H
#include <stddef.h>
#include <stdint.h>
static inline int mc_encoder_source_caller(uint32_t a){return a==0x7b93cu||a==0x7b3ffu||a==0x7beafu;}
/* Exact original ScoreName creation-before-resize and final export callers.
 * Only the Save auxiliary wrapper invokes this linear source consumer. */
static inline int mc_linear_source_caller(uint32_t a){return a==0x733f2u||a==0x734ecu||a==0x73622u||a==0x7371cu;}
#define MC_OWNER 1u
#define MC_SOURCE_BOUND 2u
#define MC_PRODUCER_DRAINED 4u
#define MC_GUEST_GENERATION 8u
#define MC_RGBA8_LINEAR 1u
typedef struct {
 uint32_t resource,data,bits,format,width,height,pitch,face,level,caller;
 uint32_t header[6],parent_header[5];
 uint64_t memory_epoch;
} McLock;
typedef struct {
 McLock lock;
 uint64_t allocation,contents,observed_allocation,observed_contents;
 uint64_t guest_generation,observed_guest_generation;
 uint64_t producer_serial,completed_serial;
 uint32_t flags,layout;const unsigned char*rgba;size_t bytes;
} McSnapshot;
/* All proof fields MUST come from actual backend producer/guest allocation
 * tracking, not the caller setting these flags because address lookup succeeds.
 * snapshot is an owned CPU copy made in one SDK93 owner job, not borrowed GPU.
 * Caller owns guest CPU turn and excludes ALL writers of dst/current header.
 * This function does not establish that exclusion or obtain GPU provenance.
 * Returns0 without destination writes on every failed precondition/allocation.
 * No fallback to stale pixels is authorized for an attempted coherent export. */
int mc_encode_rgba_morton(const unsigned char*,uint32_t,uint32_t,void*,size_t);
int mc_snapshot_matches(const McLock*,const McLock*,const McSnapshot*,void*,size_t);
int mc_same_lock(const McLock*,const McLock*);
int mc_commit(const McLock*before,const McLock*current,const McSnapshot*,void*dst,size_t capacity);
#endif
