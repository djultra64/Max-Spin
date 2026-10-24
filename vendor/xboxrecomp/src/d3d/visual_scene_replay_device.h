/* Borrow actual scene device and exact retained clone on owner only. djultra64. */
#ifndef TOPSPIN_VISUAL_SCENE_REPLAY_DEVICE_H
#define TOPSPIN_VISUAL_SCENE_REPLAY_DEVICE_H
#include "visual_scene_journal.h"
#include "visual_scene_retained_describe.h"
typedef struct {
 uint32_t version,bytes;VkPhysicalDevice physical;VkDevice device;VkQueue queue;
 uint32_t family,reserved;uint64_t device_epoch;
} D3D8SceneReplayDevice;
int d3d8_vk_scene_replay_device(D3D8SceneReplayDevice*);
int d3d8_vk_scene_replay_admitted(uint64_t device_epoch);
int d3d8_vk_scene_describe_retained(const VsjUse*,void*borrowed_lease,VksSource*);
#endif
