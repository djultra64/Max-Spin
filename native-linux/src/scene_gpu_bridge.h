/* Owned CPU-boundary to actual GPU-executor transport. djultra64. */
#ifndef TOPSPIN_SCENE_GPU_BRIDGE_H
#define TOPSPIN_SCENE_GPU_BRIDGE_H
#include <stdint.h>
typedef struct {
 uint32_t version,simulation_tick,time_bits,reserved;
 uint64_t scene,view,authority_revision,boundary_ns;
} TopspinSceneHostStamp;
/* Startup only; explicit lifecycle/snapshot/PB-lineage/recorder opt-ins required.
 * OFF does not query clocks, read guest RAM, configure GPU or issue jobs. */
int topspin_scene_gpu_init(void);
/* Actual executor only. No guest pointers in this result. Atomically revoked
 * by constructor/destructor changes or consumed-byte validation failure. */
int topspin_scene_gpu_current(TopspinSceneHostStamp*);
#endif
