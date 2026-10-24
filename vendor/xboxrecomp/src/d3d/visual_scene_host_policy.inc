/* Included after visual_scene_provider.inc, actual backend globals. djultra64. */
#include "visual_scene_host_policy.h"
int d3d8_vk_scene_host_policy(D3D8SceneHostPolicy*out){
 if(!out||!scene_ready()||!vk.swap||!vk.swap_ext.width||!vk.swap_ext.height||
  vk.swap_ext.width>INT32_MAX||vk.swap_ext.height>INT32_MAX||!vk.tw||!vk.th||
  vk.tw>INT32_MAX||vk.th>INT32_MAX||(s_keep_aspect&&(!s_aspect_num||!s_aspect_den)))return 0;
 D3D8SceneHostPolicy s={0};VkOffset3D dst[2];
 present_rect(vk.swap_ext.width,vk.swap_ext.height,dst);
 s.version=1;s.bytes=sizeof s;s.scaling=s_scaling!=0;s.keep_aspect=s_keep_aspect!=0;
 s.linear=s_linear!=0;s.vsync=s_vsync!=0;
 s.aspect[0]=s_aspect_num;s.aspect[1]=s_aspect_den;s.canvas[0]=s_canvas_num;s.canvas[1]=s_canvas_den;
 s.render_extent[0]=vk.tw;s.render_extent[1]=vk.th;
 s.swap_extent[0]=vk.swap_ext.width;s.swap_extent[1]=vk.swap_ext.height;
 s.source[3]=(int32_t)vk.tw;s.source[4]=(int32_t)vk.th;s.source[5]=1;
 for(unsigned i=0;i<2;i++){
  s.destination[3*i]=dst[i].x;s.destination[3*i+1]=dst[i].y;s.destination[3*i+2]=dst[i].z;
 }
 s.filter=s_linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST;s.gamma_enabled=s_gamma_on!=0;
 memcpy(s.gamma,s_gamma,sizeof s.gamma);*out=s;return 1;
}
