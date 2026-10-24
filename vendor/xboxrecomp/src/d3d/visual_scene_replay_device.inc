/* Bottom of actual d3d8_vk.c, after scene provider and codec106. */
#include "visual_scene_replay_device.h"
int d3d8_vk_scene_replay_admitted(uint64_t epoch){return epoch&&epoch==sn_device_generation&&scene_ready();}
int d3d8_vk_scene_replay_device(D3D8SceneReplayDevice*out){
 if(!out||!scene_ready()||!vk.phys||!vk.dev||!vk.queue||vk.qfam==UINT32_MAX||!sn_device_generation)return 0;
 D3D8SceneReplayDevice s={.version=1,.bytes=sizeof s,.physical=vk.phys,.device=vk.dev,.queue=vk.queue,.family=vk.qfam,.device_epoch=sn_device_generation};*out=s;return 1;
}
int d3d8_vk_scene_describe_retained(const VsjUse*u,void*lease,VksSource*out){
 if(!u||!out||!lease||!scene_context||!scene_ready()||u->guest_epoch!=sn_device_generation)return 0;
 return vks_describe(scene_context,&u->resource,lease,out);
}
