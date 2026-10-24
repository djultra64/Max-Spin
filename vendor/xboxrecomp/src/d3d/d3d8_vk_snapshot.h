/* PROPOSED executor-only backend exporter; djultra64. */
#ifndef TOPSPIN_D3D8_VK_SNAPSHOT_H
#define TOPSPIN_D3D8_VK_SNAPSHOT_H
#include "visual_vk_snapshot.h"
typedef int(*D3D8VkSnapshotOwner)(void*);
/* Before device creation. NULL, late configure or absent owner rejects. Actual
 * integration MUST pass nv2a_executor_is_owner93, never guestCPU ownership. */
int d3d8_vk_snapshot_configure(D3D8VkSnapshotOwner,void*);
/* Shared PB/coherence initializer, before first device only. */
int d3d8_vk_snapshot_executor_configure(void);
/* Actual current Vk bindings and exact versions, after flushing recorded work.
 * Capacity>=6. Runs only on owner; depth-only draw targets reject for now. */
int d3d8_vk_snapshot_bindings(VfpResource*,uint32_t capacity,uint32_t*count);
/* The input is an already owned/readable draw source on the GPU executor.
 * Replaces resource metadata with actual backend bindings, retains snapshots.
 * CPU material/program/geometry bytes themselves are not reinterpreted. */
int d3d8_vk_snapshot_capture(VfpBudget*,const VfpSource*,VfpPacket**);
VksContext*d3d8_vk_snapshot_context(void);
int d3d8_vk_snapshot_release(VfpPacket**);
int d3d8_vk_snapshot_close(void);
uint64_t d3d8_vk_snapshot_draw_serial(void);
int d3d8_vk_snapshot_draw_state(void*,uint32_t,uint32_t*);
#endif
