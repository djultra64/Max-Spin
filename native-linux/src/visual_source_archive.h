/* SOURCE-owned constant provenance for HOST finalization. djultra64. */
#ifndef TOPSPIN_VISUAL_SOURCE_ARCHIVE_H
#define TOPSPIN_VISUAL_SOURCE_ARCHIVE_H
#include "constant_word_lineage.h"
#include "visual_frame_packet.h"
#include "visual_scene_journal.h"
#define VSA_MAX_BYTES (32u*1024u*1024u)
#define VSA_MAX_RECORDS 8192u
typedef struct VsaArchive VsaArchive;
typedef struct {void*user;int(*owner)(void*);uint32_t records;size_t bytes;} VsaConfig;
enum {VSA_OK=1,VSA_MISSING=0,VSA_INVALID=-1,VSA_OWNER=-2,VSA_BUDGET=-3,VSA_AMBIGUOUS=-4};
VsaArchive*vsa_create(const VsaConfig*);
/* Synchronous actual SOURCE only. Copies exact compact snapshot, keyed by
 * full consumed tag/span and actual program/constant/geometry/index payloads.
 * No pointer from decoder/source/ledger survives. Actual device epoch must come from the GPU backend at SOURCE; it is
 * never attached to a CPU publication. */
int vsa_capture(VsaArchive*,const VpliPacket*,const VfpSource*,const VclSnapshot*,size_t snapshot_bytes,uint64_t actual_device_epoch);
/* Borrow valid only until retire/destroy. Does not read current mutable VCL,
 * actor memory, guest controls, clocks or GPU. Ambiguous matches reject. */
int vsa_lookup(VsaArchive*,const VsjFrame*,uint32_t command_index,const VclSnapshot**owned_snapshot);
int vsa_retire(VsaArchive*,uint64_t original_frame);
int vsa_destroy(VsaArchive**);
#endif
