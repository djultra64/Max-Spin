/* Included at end of d3d8_vk.c: uses the actual backend/registry/queue only.
 * Separate context: old single-draw sn_generation cannot revoke scene frames.
 * Own work: djultra64. */
#include "visual_scene_provider.h"
static VfpGeneration scene_continuity;
static VksContext *scene_context;
static D3D8SceneAdmission scene_admission;
static void*scene_admission_user;
static int scene_ready(void){return vk.ready&&sn_accessible()&&scene_admission&&scene_admission(scene_admission_user)==1&&scene_continuity.scene&&scene_continuity.view;}
/* A retain submits its own real copy: reject if original command buffers still
 * contain unsubmitted work. Only the outer safe PRE may close those buffers. */
static int scene_lock(void*u){(void)u;return scene_ready()&&!vk.frame_open;}
static int scene_generation(void*u,VfpGeneration*g,uint64_t*d){
 (void)u;if(!g||!d||!scene_ready())return 0;
 *g=scene_continuity;*d=sn_device_generation;return 1;
}
int d3d8_vk_scene_configure(D3D8SceneAdmission guard,void*user){
 if(vk.ready||scene_admission||!guard||!d3d8_vk_snapshot_executor_configure())return 0;
 scene_admission=guard;scene_admission_user=user;return 1;
}
int d3d8_vk_scene_continuity(VfpGeneration g){
 if(!vk.ready||!sn_accessible())return 0;
 scene_continuity=g;return 1;
}
int d3d8_vk_scene_provider(VfpProvider*out,uint64_t*device){
 if(!out||!device||!scene_ready())return 0;
 if(!scene_context){
  VksAccess a={NULL,scene_lock,sn_unlock,sn_resolve,scene_generation};
  scene_context=vks_create(vk.phys,vk.dev,vk.queue,vk.qfam,&a,64u*1024u*1024u);
  if(!scene_context)return 0;
 }
 *out=vks_provider(scene_context);*device=sn_device_generation;return 1;
}
int d3d8_vk_scene_prepare(void){
 if(!scene_ready())return 0;
 frame_submit();
 if(scene_context)vks_collect(scene_context);
 return scene_ready();
}
static int scene_binding(VkImage image,VkImageLayout layout,unsigned role,uint32_t fmt,
 uint32_t width,uint32_t height,uint32_t pitch,VfpResource*out){
 if(!image||!width||!height||!pitch)return 0;
 VksSource*s=vkr_image(&sn_versions,image);
 if(!s||!s->content_version||s->device_generation!=sn_device_generation||
  !s->width||!s->height||s->width>UINT32_MAX/4u)return 0;
 /* Physical storage dimensions can differ from the guest viewport/backing
  * (AA1/raster scale). Native args retain logical dimensions separately. */
 unsigned bytes=s->format==VK_FORMAT_R5G6B5_UNORM_PACK16||s->format==VK_FORMAT_D16_UNORM?2u:4u;
 return sn_binding(image,layout,role,fmt,s->width,s->height,s->width*bytes,out);
}
int d3d8_vk_scene_bindings(unsigned flags,VfpResource*out,unsigned capacity,unsigned*count){
 if(!out||!count||!flags||(flags&~7u)||capacity<6||!scene_ready())return 0;
 VfpResource r[6];unsigned n=0;
 uint32_t w=rt_active?rt_active->tex.width:vk.lw,h=rt_active?rt_active->tex.height:vk.lh;
 if((flags&1u)&&!(rt_active&&rt_active->depth_only)){
  if(!scene_binding(rt_active?rt_active->tex.img:vk.color,
   rt_active?rt_active->layout:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VFP_COLOR,
   rt_active?rt_active->tex.format:D3DFMT_LIN_A8R8G8B8,w,h,
   rt_active?rt_active->desc.color_pitch:w*4,&r[n]))return 0;
  n++;
 }
 if(flags&2u){
  VkImage image=rt_active?(rt_active->depth?rt_active->depth->img:VK_NULL_HANDLE):vk.depth;
  if(image){
   if(!scene_binding(image,rt_active?rt_active->depth->layout:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VFP_DEPTH,rt_active&&rt_active->depth->zformat==1?D3DFMT_LIN_D16:D3DFMT_LIN_D24S8,
    w,h,rt_active?rt_active->desc.zeta_pitch:w*4,&r[n]))return 0;
   n++;
  }
 }
 if(flags&4u){
  const NV2ACombinerState*state=d3d8_combiners_nv2a_state();
  for(unsigned stage=0;stage<4;stage++){
   VkTex*t=vk.tex[stage];
   if(t&&sx_view(t,stage)!=t->view)return 0;
   if(t&&!t->borrowed){
    if(!t->levels||t->levels>MAX_TEX_LEVELS)return 0;
    uint32_t mask=(1u<<t->levels)-1;
    if(t->cube){for(unsigned face=0;face<6;face++)if((t->face_have[face]&mask)!=mask)return 0;}
    else if((t->have&mask)!=mask)return 0;
   }
   VkImage image=t?t->img:VK_NULL_HANDLE;VkImageLayout layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   if(!image){unsigned mode=state?(state->texture_program>>(stage*5))&31u:0;image=mode==3?vk.dummy_cube:vk.dummy;}
   for(unsigned i=0;i<rt_color_n;i++)if(rt_colors[i]->tex.img==image)layout=rt_colors[i]->layout;
   for(unsigned i=0;i<rt_depth_n;i++)if(rt_depths[i]->img==image)layout=rt_depths[i]->layout;
   if(!scene_binding(image,layout,VFP_TEXTURE0+stage,t?t->format:0,t?t->width:1,t?t->height:1,t?t->pitch:4,&r[n]))return 0;
   n++;
  }
 }
 memcpy(out,r,n*sizeof*r);*count=n;return n!=0;
}
int d3d8_vk_scene_upload_resource(uint64_t image,VfpResource*out){
 if(!out||!image||!scene_ready())return 0;
 VksSource*s=vkr_image(&sn_versions,(VkImage)image);
 if(!s||!s->content_version||s->device_generation!=sn_device_generation||
  s->format!=VK_FORMAT_R8G8B8A8_UNORM||s->aspects!=VK_IMAGE_ASPECT_COLOR_BIT||s->width>UINT32_MAX/4u||
  (s->usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))return 0;
 /* Do not guess layout while an upload is recording its transitions. The
  * caller's outer PRE must snapshot the stable pre-upload registry version. */
 /* upload_level/cube_upload guarantee every pre-existing texture mip/layer is
  * SHADER_READ_ONLY between uploads. Version zero/new image is rejected: its
  * undefined contents require a separate storage-create command, not a clone. */
 s->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 *out=(VfpResource){s->storage_identity,s->content_version,VFP_TEXTURE0,0,s->width,s->height,s->width*4};
 return 1;
}
int d3d8_vk_scene_completed_resource(uint32_t address,VfpResource*out){
 if(!out||!scene_ready())return 0;
 if(!address){
  if(rt_presented)return 0;
  return scene_binding(vk.color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
   VFP_COLOR,D3DFMT_LIN_A8R8G8B8,vk.lw,vk.lh,vk.lw*4,out);
 }
 RtColor*c=rt_find(address);
 if(!c||rt_presented!=c||c->depth_only||c->color_bytes!=4)return 0;
 return scene_binding(c->tex.img,c->layout,VFP_COLOR,c->tex.format,
  c->tex.width,c->tex.height,c->desc.color_pitch,out);
}
int d3d8_vk_scene_view_state(D3D8SceneViewState*out){
 if(!out||!scene_ready()||!vk.lw||!vk.lh||!vk.tw||!vk.th)return 0;
 uint32_t lw=rt_active?rt_active->desc.width:vk.lw,lh=rt_active?rt_active->desc.height:vk.lh;
 uint32_t rw=rt_active?rt_active->raster_width:vk.tw,rh=rt_active?rt_active->raster_height:vk.th;
 if(!lw||!lh||!rw||!rh)return 0;
 VkRect2D sc=draw_scissor();D3D8SceneViewState s={0};
 s.version=1;s.bytes=sizeof s;s.depth_only=rt_active&&rt_active->depth_only;
 s.logical[0]=lw;s.logical[1]=lh;s.physical[0]=rw;s.physical[1]=rh;
 s.backbuffer_logical[0]=vk.lw;s.backbuffer_logical[1]=vk.lh;
 s.backbuffer_physical[0]=vk.tw;s.backbuffer_physical[1]=vk.th;
 memcpy(s.guest_scissor,vk.scissor,sizeof s.guest_scissor);
 s.scissor_xy[0]=sc.offset.x;s.scissor_xy[1]=sc.offset.y;
 s.scissor_wh[0]=sc.extent.width;s.scissor_wh[1]=sc.extent.height;
 /* Exactly pass_begin's Vulkan viewport, not the guest's baked NV2A camera. */
 s.viewport[1]=(float)rh;s.viewport[2]=(float)rw;s.viewport[3]=-(float)rh;s.viewport[5]=1.0f;
 s.canvas_scale[0]=d3d8_GetCanvasScaleX();s.canvas_scale[1]=d3d8_GetCanvasScaleY();
 s.placement[0]=s_place_kx;s.placement[1]=s_place_bx;*out=s;return 1;
}
int d3d8_vk_scene_close(void){
 if(!vk.ready||!sn_accessible())return 0;
 if(scene_context){vks_collect(scene_context);if(!vks_destroy(&scene_context))return 0;}
 scene_continuity=(VfpGeneration){0,0};return 1;
}
