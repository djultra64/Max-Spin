/* Included once by the mapping owner, xbox_memory_layout.c. djultra64. */
#include "guest_memory_arena_epoch.h"
#include <stdatomic.h>
static _Atomic uint64_t guest_arena_serial;
static _Atomic uint64_t guest_arena_published;
uint64_t xbox_GuestMemoryArenaEpoch(void)
{
    return atomic_load_explicit(&guest_arena_published, memory_order_acquire);
}
static void guest_memory_arena_revoke(void)
{
    atomic_store_explicit(&guest_arena_published, 0, memory_order_release);
}
static void guest_memory_arena_publish(void)
{
    uint64_t previous = atomic_load_explicit(&guest_arena_serial, memory_order_relaxed);
    /* Exhaustion must never resurrect an old mapping identity or wrap to one.
     * Original mapping success remains unchanged; optional readers fail closed. */
    guest_memory_arena_revoke();
    while (previous != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&guest_arena_serial, &previous,
                previous + 1, memory_order_relaxed, memory_order_relaxed)) {
            atomic_store_explicit(&guest_arena_published, previous + 1, memory_order_release);
            return;
        }
    }
}
