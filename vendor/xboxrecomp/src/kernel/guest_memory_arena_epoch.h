/* Guest mapping incarnation, independent of heap/contiguous allocations.
 * Own work djultra64. */
#ifndef XBOX_GUEST_MEMORY_ARENA_EPOCH_H
#define XBOX_GUEST_MEMORY_ARENA_EPOCH_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Acquire snapshot: zero before successful layout initialization and from
 * the first instruction of shutdown onward. Never a guest address or guessed
 * constant. Does not pin pages: use the actual guest owner/lifecycle guard
 * and recheck after a bounded observation. Init/shutdown remain serialized
 * by their existing caller contract; this API adds no mapping lock. */
uint64_t xbox_GuestMemoryArenaEpoch(void);
#ifdef __cplusplus
}
#endif
#endif
