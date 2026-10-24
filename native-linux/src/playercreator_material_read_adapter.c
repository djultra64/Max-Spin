/* Proposed root adapter: all renderer work executes through SDK93.
 * Own work: djultra64. No Vulkan from the invoking guest thread. */
#include "playercreator_material_read_trace.h"
#include "nv2a_executor_job.h"
#include "d3d/d3d8_vk_rt.h"
#include <string.h>
static int material_work(const void*input,uint32_t in,void*output,uint32_t out){
 if(!nv2a_executor_is_owner()||in!=sizeof(TopspinMaterialReadRequest)||out!=sizeof(TopspinMaterialReadSnapshot))return 0;
 const TopspinMaterialReadRequest*q=input;TopspinMaterialReadSnapshot*s=output;s->selected=-1;
 if(!q->width||q->width>512||!q->height||q->height>512)return 0;
 for(unsigned i=0;i<2;i++){
  if(i&&q->addresses[0]==q->addresses[1])continue;
  D3D8VkRasterExtent e;
  s->known[i]=d3d8_vk_rt_extent(q->addresses[i],&e);
  if(s->known[i]==1){s->widths[i]=e.guest_width;s->heights[i]=e.guest_height;
   if(e.guest_width==q->width&&e.guest_height==q->height){if(s->selected!=-1){s->selected=-2;return 1;}s->selected=(int)i;}
  }
 }
 if(s->selected>=0)s->gpu_readback=d3d8_vk_rt_readback(q->addresses[s->selected],s->rgba,q->width*4);
 /* Existing API has no allocation generation/version. Explicitly unknown. */
 return 1;
}
int topspin_creator_material_gpu_capture(const TopspinMaterialReadRequest*q,TopspinMaterialReadSnapshot*s){
 int work_result=0;
 int status=nv2a_executor_invoke(material_work,q,sizeof*q,s,sizeof*s,2000,&work_result);
 return status==NV2A_EXECUTOR_DONE&&work_result==1?1:status==NV2A_EXECUTOR_DONE?NV2A_EXECUTOR_INVALID:status;
}
