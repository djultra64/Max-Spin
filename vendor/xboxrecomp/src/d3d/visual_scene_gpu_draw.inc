/* Bottom of actual d3d8_vk.c, after provider. No original work is re-issued.
 * Only original process-lifetime immutable handles may be borrowed. djultra64. */
#include "visual_scene_gpu_draw.h"
_Static_assert(sizeof(PipeKey)==32,"Re-audit original canonical pipeline key ABI");
_Static_assert(sizeof(NV2APSConstants)<=D3D8_SCENE_PS_MAX,"PS constant codec capacity");
_Static_assert(sizeof(D3D8SceneGpuDraw)<=8192,"Bounded owned draw codec");
static uint64_t sgd_handle_pipeline(VkPipeline p){return(uint64_t)(uintptr_t)p;}
static const PipeEntry*sgd_pipeline(uint64_t handle){
 if(!handle)return NULL;
 const PipeEntry*found=NULL;
 for(unsigned i=0;i<PIPE_SLOTS;i++)if(vk.pipes[i].used&&vk.pipes[i].pipe&&sgd_handle_pipeline(vk.pipes[i].pipe)==handle){
  if(found&&memcmp(&found->key,&vk.pipes[i].key,sizeof(PipeKey)))return NULL;
  found=&vk.pipes[i];
 }
 return found;
}
static int sgd_sampler(uint64_t handle){
 if(!handle)return 0;
 /* Byte access covers the full actual multidimensional array without C
  * pointer arithmetic crossing its innermost subarray object. */
 for(size_t i=0;i<sizeof vk.samplers/sizeof(VkSampler);i++){
  VkSampler value;memcpy(&value,(const unsigned char*)vk.samplers+i*sizeof value,sizeof value);
  if((uint64_t)(uintptr_t)value==handle)return 1;
 }
 return 0;
}
int d3d8_vk_scene_resolved_draw_validate(const D3D8SceneGpuDraw*s){
 if(!s||!scene_ready()||s->version!=D3D8_SCENE_GPU_DRAW_VERSION||s->bytes!=sizeof*s||s->ps_bytes!=sizeof(NV2APSConstants)||
    !s->device_epoch||s->device_epoch!=sn_device_generation||!s->pipeline_layout||!s->descriptor_layout||
    s->pipeline_layout!=(uint64_t)(uintptr_t)vk.pl||s->descriptor_layout!=(uint64_t)(uintptr_t)vk.dsl||
    s->depth_format!=(uint32_t)vk.depth_fmt||s->cube_mask&~15u||
    s->stencil_reference>255||s->stencil_compare_mask>255||s->stencil_write_mask>255)return 0;
 const PipeEntry*entry=sgd_pipeline(s->pipeline);if(!entry||memcmp(&entry->key,s->pipeline_key,sizeof entry->key))return 0;
 const PipeKey*k=&entry->key;
 if(!k->vs||!k->ps||s->vertex_shader!=k->vs||s->pixel_shader!=k->ps||s->layout!=k->layout||
    s->instanced!=k->instanced||s->topology!=k->topology||s->color_format!=k->color_format||
    s->has_depth!=k->has_depth||s->stencil_enabled!=k->stencil)return 0;
 for(unsigned i=0;i<4;i++)if(!sgd_sampler(s->samplers[i]))return 0;
 return 1;
}
int d3d8_vk_scene_resolved_draw(void*out,uint32_t cap,uint32_t*bytes){
 if(!out||!bytes||cap<sizeof(D3D8SceneGpuDraw)||!scene_ready()||!vk.frame_open||!vk.bound_set||!vk.ubo.map||
    (uint64_t)vk.dyn_off[0]+sizeof(NV2APSConstants)>vk.ubo.size||
    (uint64_t)vk.dyn_off[2]+12*sizeof(float)>vk.ubo.size)return 0;
 const PipeEntry*entry=sgd_pipeline(sgd_handle_pipeline(vk.bound_pipe));if(!entry)return 0;
 const PipeKey*k=&entry->key;D3D8SceneGpuDraw s={0};
 s.version=D3D8_SCENE_GPU_DRAW_VERSION;s.bytes=sizeof s;s.ps_bytes=sizeof(NV2APSConstants);s.depth_format=vk.depth_fmt;
 s.device_epoch=sn_device_generation;s.pipeline=sgd_handle_pipeline(vk.bound_pipe);
 s.pipeline_layout=(uint64_t)(uintptr_t)vk.pl;s.descriptor_layout=(uint64_t)(uintptr_t)vk.dsl;
 memcpy(s.pipeline_key,k,sizeof*k);s.vertex_shader=k->vs;s.pixel_shader=k->ps;s.layout=k->layout;
 s.instanced=k->instanced;s.topology=k->topology;s.color_format=k->color_format;s.has_depth=k->has_depth;s.stencil_enabled=k->stencil;
 for(unsigned i=0;i<4;i++){
  s.samplers[i]=(uint64_t)(uintptr_t)vk.set_samplers[i];VkImageView actual=vk.set_views[i];VkTex*t=vk.tex[i];
  if(!actual)return 0;
  if(t&&t->img){if(actual!=t->view)return 0;if(t->cube)s.cube_mask|=1u<<i;}
  else if(actual==vk.dummy_cube_view)s.cube_mask|=1u<<i;
  else if(actual!=vk.dummy_view)return 0;
 }
 s.stencil_reference=vk.rs[D3DRS_STENCILREF]&255u;s.stencil_compare_mask=vk.rs[D3DRS_STENCILMASK]&255u;s.stencil_write_mask=vk.rs[D3DRS_STENCILWRITEMASK]&255u;
 s.depth_bias[0]=vk.polygon_units;
 if(rt_active&&((rt_active->desc.format>>4)&15u)==1)s.depth_bias[0]*=16777215.0f/65535.0f;
 s.depth_bias[1]=0.0f;s.depth_bias[2]=vk.polygon_slope;memcpy(s.blend,vk.blend_color,sizeof s.blend);
 memcpy(s.ps_constants,vk.ubo.map+vk.dyn_off[0],sizeof(NV2APSConstants));
 memcpy(s.frame_constants,vk.ubo.map+vk.dyn_off[2],sizeof s.frame_constants);
 if(!d3d8_vk_scene_resolved_draw_validate(&s))return 0;
 memcpy(out,&s,sizeof s);*bytes=sizeof s;return 1;
}
