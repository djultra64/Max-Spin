/* Explicit same-window consumer. Private commands/descriptors/gamma resources.
 * No SDL/window operations, guest calls, original render globals or clocks.
 * Own work: djultra64. */
#include "visual_scene_present.h"
#include "visual_scene_source_session.h"
#include "visual_scene_present_gamma_spv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define LIMIT (64ull*1024*1024)
#define ACQUIRE(type,destination,call) do{type acquired=VK_NULL_HANDLE;if((call)!=VK_SUCCESS)return 0;(destination)=acquired;}while(0)
typedef struct{VkImage image;
 VkImageView view;
 VkDeviceMemory memory;
 VkImageLayout layout;
 } Image;
 
typedef struct{
 VreContext*source;
 VreOutput input;
 uint64_t hold,ticket;
 VsrSourceTag source_tag;D3D8SceneHostPolicy policy;uint64_t session;
 VspWsiLease wsi;
 
 VkCommandPool pool;
 VkCommandBuffer cmd;
 VkFence fence;
 VkSemaphore acquire;
 
 VkDescriptorPool descriptors;
 VkDescriptorSet set;
 VkBuffer buffer;
 VkDeviceMemory memory;
 void*mapped;
 VkFramebuffer framebuffer;
 
 Image normalized,gamma;
 VkDeviceSize charged;
 uint32_t image_index;
 
 int acquired,submitted,discard,complete,presented,retired,lost,refresh;
 
} Job;
 
struct VspContext{
 VspConfig config;
 VkPhysicalDeviceMemoryProperties memory;
 VkShaderModule vertex,pixel;
 
 VkDescriptorSetLayout dsl;
 VkPipelineLayout pl;
 VkRenderPass pass;
 VkPipeline pipeline;
 
 uint64_t nonce;
 int busy;
 Job*job;
 
};
 
static int enter(VspContext*c){if(!c||c->busy)return 0;
 c->busy=1;
 if(!c->config.owner(c->config.user)){c->busy=0;
 return 0;
 }return 1;
 }
static int leave(VspContext*c,int r){c->busy=0;
 return r;
 }
static int admitted(VspContext*c){return c->config.admitted(c->config.user,c->config.device_epoch);
 }
static int memory_type(VspContext*c,uint32_t mask,VkMemoryPropertyFlags flags,uint32_t*out){for(unsigned i=0;i<c->memory.memoryTypeCount;i++)if((mask&(1u<<i))&&(c->memory.memoryTypes[i].propertyFlags&flags)==flags){*out=i;
 return 1;
 }return 0;
 }
static int allocate(VspContext*c,Job*j,const VkMemoryRequirements*r,VkMemoryPropertyFlags flags,VkDeviceMemory*out){
 uint32_t type;
 if(!r->size||r->size>c->config.gpu_budget||j->charged>c->config.gpu_budget-r->size||!((!flags&&memory_type(c,r->memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type))||memory_type(c,r->memoryTypeBits,flags,&type)))return 0;
 
 VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r->size,.memoryTypeIndex=type};
 ACQUIRE(VkDeviceMemory,*out,vkAllocateMemory(c->config.device,&a,NULL,&acquired));
 j->charged+=r->size;
 return 1;
 
}
static int image_create(VspContext*c,Job*j,Image*out,uint32_t w,uint32_t h){
 VkImageCreateInfo a={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 
 ACQUIRE(VkImage,out->image,vkCreateImage(c->config.device,&a,NULL,&acquired));
 VkMemoryRequirements r;
 vkGetImageMemoryRequirements(c->config.device,out->image,&r);
 
 if(!allocate(c,j,&r,0,&out->memory)||vkBindImageMemory(c->config.device,out->image,out->memory,0)!=VK_SUCCESS)return 0;
 
 VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 
 ACQUIRE(VkImageView,out->view,vkCreateImageView(c->config.device,&view,NULL,&acquired));
 out->layout=VK_IMAGE_LAYOUT_UNDEFINED;
 return 1;
 
}
static void barrier(Job*j,Image*i,VkImageLayout layout){
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=i->layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.oldLayout=i->layout,.newLayout=layout,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=i->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 
 vkCmdPipelineBarrier(j->cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
 i->layout=layout;
 
}
static void destroy_image(VspContext*c,Image*i){if(i->view)vkDestroyImageView(c->config.device,i->view,NULL);
 if(i->image)vkDestroyImage(c->config.device,i->image,NULL);
 if(i->memory)vkFreeMemory(c->config.device,i->memory,NULL);
 }
static void destroy_job(VspContext*c,Job*j){
 VkDevice d=c->config.device;
 if(j->pool)vkDestroyCommandPool(d,j->pool,NULL);
 if(j->framebuffer)vkDestroyFramebuffer(d,j->framebuffer,NULL);
 if(j->descriptors)vkDestroyDescriptorPool(d,j->descriptors,NULL);
 destroy_image(c,&j->normalized);
 destroy_image(c,&j->gamma);
 
 if(j->mapped)vkUnmapMemory(d,j->memory);
 if(j->buffer)vkDestroyBuffer(d,j->buffer,NULL);
 if(j->memory)vkFreeMemory(d,j->memory,NULL);
 if(j->acquire)vkDestroySemaphore(d,j->acquire,NULL);
 if(j->fence)vkDestroyFence(d,j->fence,NULL);
 free(j);
 
}
static int release_holds(VspContext*c,Job*j){
 if(j->wsi.token){if(!c->config.wsi_release(c->config.user,j->wsi.token))return 0;
 j->wsi.token=0;
 }
 if(j->hold){if(vre_consumer_release(j->source,j->input.ticket,j->hold)!=VRE_OK)return 0;
 j->hold=0;
 }return 1;
 
}
static void destroy_context(VspContext*c){VkDevice d=c->config.device;
 if(c->pipeline)vkDestroyPipeline(d,c->pipeline,NULL);
 if(c->pass)vkDestroyRenderPass(d,c->pass,NULL);
 if(c->pl)vkDestroyPipelineLayout(d,c->pl,NULL);
 if(c->dsl)vkDestroyDescriptorSetLayout(d,c->dsl,NULL);
 if(c->vertex)vkDestroyShaderModule(d,c->vertex,NULL);
 if(c->pixel)vkDestroyShaderModule(d,c->pixel,NULL);
 free(c);
 }
static int gamma_create(VspContext*c){
 VkDevice d=c->config.device;
 VkShaderModuleCreateInfo shader={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof vsp_gamma_vs,.pCode=vsp_gamma_vs};
 ACQUIRE(VkShaderModule,c->vertex,vkCreateShaderModule(d,&shader,NULL,&acquired));
 shader.codeSize=sizeof vsp_gamma_ps;
 shader.pCode=vsp_gamma_ps;
 ACQUIRE(VkShaderModule,c->pixel,vkCreateShaderModule(d,&shader,NULL,&acquired));
 
 VkDescriptorSetLayoutBinding b[]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},{8,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}};
 VkDescriptorSetLayoutCreateInfo dl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=b};
 ACQUIRE(VkDescriptorSetLayout,c->dsl,vkCreateDescriptorSetLayout(d,&dl,NULL,&acquired));
 
 VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&c->dsl};
 ACQUIRE(VkPipelineLayout,c->pl,vkCreatePipelineLayout(d,&pl,NULL,&acquired));
 
 VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
 VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
 VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub};
 ACQUIRE(VkRenderPass,c->pass,vkCreateRenderPass(d,&rp,NULL,&acquired));
 
 VkPipelineShaderStageCreateInfo stages[]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=c->vertex,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=c->pixel,.pName="main"}};
 
 VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
 VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
 VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1.f};
 VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
 VkPipelineColorBlendAttachmentState att={.colorWriteMask=15};
 VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&att};
 VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
 VkPipelineDynamicStateCreateInfo dy={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamic};
 VkGraphicsPipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,.pDynamicState=&dy,.layout=c->pl,.renderPass=c->pass};
 ACQUIRE(VkPipeline,c->pipeline,vkCreateGraphicsPipelines(d,VK_NULL_HANDLE,1,&pipe,NULL,&acquired));
 return 1;
 
}
static int resources(VspContext*c,Job*j){
 VkDevice d=c->config.device;
 VkCommandPoolCreateInfo p={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=c->config.family};
 ACQUIRE(VkCommandPool,j->pool,vkCreateCommandPool(d,&p,NULL,&acquired));
 VkCommandBufferAllocateInfo cmd={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=j->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
 ACQUIRE(VkCommandBuffer,j->cmd,vkAllocateCommandBuffers(d,&cmd,&acquired));
 VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 ACQUIRE(VkFence,j->fence,vkCreateFence(d,&fence,NULL,&acquired));
 VkSemaphoreCreateInfo sem={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
 ACQUIRE(VkSemaphore,j->acquire,vkCreateSemaphore(d,&sem,NULL,&acquired));
 
 uint32_t w=j->policy.render_extent[0],h=j->policy.render_extent[1];
 if(!image_create(c,j,&j->normalized,w,h))return 0;
 if(!j->policy.gamma_enabled)return 1;
 
 if(!image_create(c,j,&j->gamma,w,h))return 0;
 
 VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=c->pass,.attachmentCount=1,.pAttachments=&j->gamma.view,.width=w,.height=h,.layers=1};
 ACQUIRE(VkFramebuffer,j->framebuffer,vkCreateFramebuffer(d,&fb,NULL,&acquired));
 
 VkBufferCreateInfo buffer={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=sizeof j->policy.gamma,.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 ACQUIRE(VkBuffer,j->buffer,vkCreateBuffer(d,&buffer,NULL,&acquired));
 VkMemoryRequirements mr;
 vkGetBufferMemoryRequirements(d,j->buffer,&mr);
 if(!allocate(c,j,&mr,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&j->memory)||vkBindBufferMemory(d,j->buffer,j->memory,0)!=VK_SUCCESS)return 0;
 
 void*map=NULL;
 if(vkMapMemory(d,j->memory,0,sizeof j->policy.gamma,0,&map)!=VK_SUCCESS)return 0;
 j->mapped=map;
 memcpy(map,j->policy.gamma,sizeof j->policy.gamma);
 
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1}};
 VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
 ACQUIRE(VkDescriptorPool,j->descriptors,vkCreateDescriptorPool(d,&pool,NULL,&acquired));
 VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=j->descriptors,.descriptorSetCount=1,.pSetLayouts=&c->dsl};
 ACQUIRE(VkDescriptorSet,j->set,vkAllocateDescriptorSets(d,&ai,&acquired));
 
 VkDescriptorBufferInfo bi={j->buffer,0,sizeof j->policy.gamma};
 VkDescriptorImageInfo ii={.imageView=j->normalized.view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
 VkWriteDescriptorSet writes[]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=j->set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=j->set,.dstBinding=8,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.pImageInfo=&ii}};
 vkUpdateDescriptorSets(d,2,writes,0,NULL);
 return 1;
 
}
static void destination(const D3D8SceneHostPolicy*,const VspWsiLease*,VkOffset3D[2]);
static int policy_valid(const VsrSourceTag*h,const D3D8SceneHostPolicy*p,const VsjStamp*s){
 if(h->version!=1||h->reserved||!h->authority_revision||!h->boundary_ns||h->scene!=s->continuity.scene||h->view!=s->continuity.view||h->simulation_tick!=s->simulation_tick||p->version!=1||p->bytes!=sizeof*p||!p->render_extent[0]||!p->render_extent[1]||p->render_extent[0]>16384||p->render_extent[1]>16384||p->keep_aspect>1||p->linear>1||p->vsync>1||p->gamma_enabled>1||(p->keep_aspect&&(!p->aspect[0]||!p->aspect[1]))||p->filter!=(p->linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST))return 0;
 
 if(p->source[0]||p->source[1]||p->source[2]||p->source[3]!=(int32_t)p->render_extent[0]||p->source[4]!=(int32_t)p->render_extent[1]||p->source[5]!=1)return 0;
 
 if(!p->swap_extent[0]||!p->swap_extent[1]||p->swap_extent[0]>INT32_MAX||p->swap_extent[1]>INT32_MAX)return 0;
 VspWsiLease captured={.width=p->swap_extent[0],.height=p->swap_extent[1]};
 VkOffset3D exact[2];destination(p,&captured,exact);
 if(exact[0].x>=exact[1].x||exact[0].y>=exact[1].y||memcmp(p->destination,exact,sizeof exact))return 0;
 if(p->gamma_enabled)for(unsigned i=0;i<256;i++)for(unsigned j=0;j<4;j++)if(!isfinite(p->gamma[i][j])||p->gamma[i][j]<0||p->gamma[i][j]>1)return 0;
 return 1;
 
}
static int same_output(const VreOutput*a,const VreOutput*b){return a->ticket==b->ticket&&a->image==b->image&&a->view==b->view&&a->layout==b->layout&&a->format==b->format&&a->width==b->width&&a->height==b->height&&a->fence==b->fence;
 }
static void destination(const D3D8SceneHostPolicy*p,const VspWsiLease*w,VkOffset3D dst[2]){
 uint32_t width=w->width,height=w->height;
 if(p->keep_aspect){if((uint64_t)width*p->aspect[1]>(uint64_t)height*p->aspect[0])width=(uint32_t)((uint64_t)height*p->aspect[0]/p->aspect[1]);
 else height=(uint32_t)((uint64_t)width*p->aspect[1]/p->aspect[0]);
 }dst[0]=(VkOffset3D){(int32_t)((w->width-width)/2),(int32_t)((w->height-height)/2),0};
 dst[1]=(VkOffset3D){dst[0].x+(int32_t)width,dst[0].y+(int32_t)height,1};
 
}
static int record(VspContext*c,Job*j){
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
 if(vkBeginCommandBuffer(j->cmd,&begin)!=VK_SUCCESS)return 0;
 
 uint32_t w=j->policy.render_extent[0],h=j->policy.render_extent[1];
 barrier(j,&j->normalized,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 
 if(j->input.format==VK_FORMAT_R8G8B8A8_UNORM&&j->input.width==w&&j->input.height==h){VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.extent={w,h,1}};
 vkCmdCopyImage(j->cmd,j->input.image,j->input.layout,j->normalized.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 }
 else{VkImageBlit copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffsets={{0,0,0},{(int32_t)j->input.width,(int32_t)j->input.height,1}},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffsets={{0,0,0},{(int32_t)w,(int32_t)h,1}}};
 vkCmdBlitImage(j->cmd,j->input.image,j->input.layout,j->normalized.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy,VK_FILTER_LINEAR);
 }
 Image*from=&j->normalized;
 if(j->policy.gamma_enabled){barrier(j,&j->normalized,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 barrier(j,&j->gamma,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
 VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=c->pass,.framebuffer=j->framebuffer,.renderArea={{0,0},{w,h}}};
 vkCmdBeginRenderPass(j->cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);
 VkViewport viewport={0,0,(float)w,(float)h,0,1};
 VkRect2D scissor={{0,0},{w,h}};
 vkCmdSetViewport(j->cmd,0,1,&viewport);
 vkCmdSetScissor(j->cmd,0,1,&scissor);
 vkCmdBindPipeline(j->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,c->pipeline);
 vkCmdBindDescriptorSets(j->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,c->pl,0,1,&j->set,0,NULL);
 vkCmdDraw(j->cmd,3,1,0,0);
 vkCmdEndRenderPass(j->cmd);
 j->gamma.layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
 from=&j->gamma;
 }
 barrier(j,from,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 Image swap={.image=j->wsi.images[j->image_index],.layout=VK_IMAGE_LAYOUT_UNDEFINED};
 barrier(j,&swap,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 VkClearColorValue black={{0,0,0,1}};
 VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 vkCmdClearColorImage(j->cmd,swap.image,swap.layout,&black,1,&range);
 /* Clear and blit both write the acquired image; same-layout dependency is
  * still required for their overlapping transfer writes. */
 barrier(j,&swap,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 
 VkImageBlit blit={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffsets={{0,0,0},{(int32_t)w,(int32_t)h,1}},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}};
 destination(&j->policy,&j->wsi,blit.dstOffsets);
 vkCmdBlitImage(j->cmd,from->image,from->layout,swap.image,swap.layout,1,&blit,(VkFilter)j->policy.filter);
 barrier(j,&swap,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 return vkEndCommandBuffer(j->cmd)==VK_SUCCESS;
 
}
static int publish_job(VspContext*c,Job*j,VspOutput*out,int lost){j->ticket=++c->nonce;
 j->lost=lost;
 c->job=j;
 *out=(VspOutput){j->ticket,j->input.ticket,j->fence};
 return leave(c,lost?VSP_QUARANTINED:VSP_OK);
 }
/* A failed exact-token release is ambiguous ownership, even before submission.
 * Preserve the real holds/resources as a quarantined ticket; never claim completion. */
static int rollback(VspContext*c,Job*j,VspOutput*out,int result){
 if(!release_holds(c,j))return publish_job(c,j,out,1);
 destroy_job(c,j);return leave(c,result);
}
static int drain(VspContext*c,Job*j,VspOutput*out){
 j->discard=1;
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&j->acquire,.pWaitDstStageMask=&stage};
 VkResult result=vkQueueSubmit(c->config.queue,1,&submit,j->fence);
 j->submitted=result==VK_SUCCESS;
 return publish_job(c,j,out,result!=VK_SUCCESS);
 
}
VspContext*vsp_create(const VspConfig*config){
 if(!config||!config->owner||!config->admitted||!config->wsi_borrow||!config->wsi_valid||!config->wsi_release||!config->wsi_retire||!config->wsi_refresh||!config->physical||!config->device||!config->queue||!config->device_epoch||!config->gpu_budget||config->gpu_budget>LIMIT||!config->owner(config->user)||!config->admitted(config->user,config->device_epoch))return NULL;
 
 VspContext*c=calloc(1,sizeof*c);
 if(!c)return NULL;
 c->config=*config;
 vkGetPhysicalDeviceMemoryProperties(config->physical,&c->memory);
 if(!c->memory.memoryTypeCount||c->memory.memoryTypeCount>32||!gamma_create(c)){destroy_context(c);
 return NULL;
 }return c;
 
}
static int session_valid(VspContext*c,uint64_t session){return !session||d3d8_vk_scene_source_validate(session,c->config.device_epoch)==1;}
static int submit(VspContext*c,VreContext*source,const VreOutput*input,const VsrSourceTag*tag,const D3D8SceneHostPolicy*policy,uint64_t session,int valid_header,VspOutput*out){
 if(!enter(c))return VSP_OWNER;
 if(c->job)return leave(c,VSP_BUSY);
 if(!source||!input||!tag||!policy||!out||!valid_header||!admitted(c)||c->nonce==UINT64_MAX||!session_valid(c,session))return leave(c,VSP_INVALID);
 
 int ready=vre_poll(source,input->ticket);
 if(ready==VRE_WAIT)return leave(c,VSP_WAIT);
 if(ready!=VRE_OK)return leave(c,VSP_INVALID);
 VreOutput actual;
 VsjStamp stamp;
 
 if(vre_output(source,input->ticket,&actual)!=VRE_OK||!same_output(input,&actual)||vre_source_stamp(source,input->ticket,&stamp)!=VRE_OK||stamp.device_epoch!=c->config.device_epoch||!policy_valid(tag,policy,&stamp)||!input->image||!input->view||input->layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL||!input->width||!input->height||input->width>16384||input->height>16384)return leave(c,VSP_INVALID);
 
 Job*j=calloc(1,sizeof*j);
 if(!j)return leave(c,VSP_GPU);
 j->source=source;
 j->input=actual;
 j->source_tag=*tag;j->policy=*policy;j->session=session;
 
 if(vre_consumer_acquire(source,input->ticket,&j->hold)!=VRE_OK){free(j);
 return leave(c,VSP_INVALID);
 }
 if(!c->config.wsi_borrow(c->config.user,&j->wsi)||j->wsi.version!=1||j->wsi.bytes!=sizeof j->wsi||!j->wsi.token||!j->wsi.swap_epoch||!j->wsi.swap||!j->wsi.surface||!j->wsi.count||j->wsi.count>VSP_WSI_IMAGES||!j->wsi.width||!j->wsi.height||j->wsi.width>INT32_MAX||j->wsi.height>INT32_MAX||j->wsi.device_epoch!=c->config.device_epoch||!c->config.wsi_valid(c->config.user,&j->wsi)){return rollback(c,j,out,VSP_GPU);
 }
 VkOffset3D display[2];destination(&j->policy,&j->wsi,display);
 if(display[0].x>=display[1].x||display[0].y>=display[1].y)return rollback(c,j,out,VSP_INVALID);
 if(!resources(c,j))return rollback(c,j,out,VSP_GPU);
 uint32_t image=UINT32_MAX;
 VkResult result=vkAcquireNextImageKHR(c->config.device,j->wsi.swap,0,j->acquire,VK_NULL_HANDLE,&image);
 
 if(result==VK_NOT_READY||result==VK_TIMEOUT||result==VK_ERROR_OUT_OF_DATE_KHR){int refresh=result==VK_ERROR_OUT_OF_DATE_KHR;
 if(!release_holds(c,j))return publish_job(c,j,out,1);
 destroy_job(c,j);
 if(refresh)(void)c->config.wsi_refresh(c->config.user);
 return leave(c,VSP_WAIT);
 }
 if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)return publish_job(c,j,out,1);
 
 j->refresh=result==VK_SUBOPTIMAL_KHR;
 j->acquired=1;
 j->image_index=image;
 
 if(image>=j->wsi.count||!admitted(c)||!session_valid(c,j->session)||!c->config.wsi_valid(c->config.user,&j->wsi)||!record(c,j))return drain(c,j,out);
 
 if(!session_valid(c,j->session))return drain(c,j,out);
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&j->acquire,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&j->cmd};
 result=vkQueueSubmit(c->config.queue,1,&submit,j->fence);
 
 if(result==VK_ERROR_DEVICE_LOST)return publish_job(c,j,out,1);
 if(result!=VK_SUCCESS)return drain(c,j,out);
 j->submitted=1;
 return publish_job(c,j,out,0);
 
}
int vsp_submit(VspContext*c,VreContext*source,const VreOutput*input,const VsrHostPolicy*policy,VspOutput*out){
 return submit(c,source,input,policy?&policy->source:NULL,policy?&policy->policy:NULL,0,policy&&policy->version==1,out);
}
int vsp_submit_source_only(VspContext*c,VreContext*source,const VreOutput*input,const VsrSourceOnlyPolicy*policy,VspOutput*out){
 int valid=policy&&policy->version==1&&!policy->reserved&&policy->display.version==1&&policy->display.bytes==sizeof policy->display&&policy->display.session_id;
 return submit(c,source,input,policy?&policy->source:NULL,policy?&policy->display.policy:NULL,policy?policy->display.session_id:0,valid,out);
}
int vsp_poll(VspContext*c,uint64_t ticket){
 if(!enter(c))return VSP_OWNER;
 Job*j=c->job;
 if(!j||!ticket||ticket!=j->ticket)return leave(c,VSP_INVALID);
 if(j->lost)return leave(c,VSP_LOST);
 if(j->presented||j->retired)return leave(c,VSP_OK);
 
 if(!j->complete){VkResult status=vkGetFenceStatus(c->config.device,j->fence);
 if(status==VK_NOT_READY)return leave(c,VSP_WAIT);
 if(status!=VK_SUCCESS){j->lost=1;
 return leave(c,VSP_LOST);
 }j->complete=1;
 }
 VreOutput actual;
 if(j->discard||!admitted(c)||!session_valid(c,j->session)||vre_output(j->source,j->input.ticket,&actual)!=VRE_OK||!same_output(&actual,&j->input)||!c->config.wsi_valid(c->config.user,&j->wsi)){
  if(!c->config.wsi_retire(c->config.user,j->wsi.token))return leave(c,VSP_WAIT);
 j->wsi.token=0;
 j->retired=1;
 return leave(c,VSP_OK);
 
 }
 VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.swapchainCount=1,.pSwapchains=&j->wsi.swap,.pImageIndices=&j->image_index};
 VkResult result=vkQueuePresentKHR(c->config.queue,&present);
 
 if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR&&result!=VK_ERROR_OUT_OF_DATE_KHR){j->lost=1;
 return leave(c,VSP_LOST);
 }j->refresh|=result==VK_SUBOPTIMAL_KHR||result==VK_ERROR_OUT_OF_DATE_KHR;
 j->presented=1;
 return leave(c,VSP_OK);
 
}
int vsp_finish(VspContext*c,uint64_t ticket){
 int result=vsp_poll(c,ticket);
 if(result!=VSP_OK)return result;
 if(!enter(c))return VSP_OWNER;
 Job*j=c->job;
 if(!j||j->ticket!=ticket||!j->complete||(!j->presented&&!j->retired))return leave(c,VSP_INVALID);
 if(!release_holds(c,j))return leave(c,VSP_BUSY);
 int refresh=j->refresh;
 c->job=NULL;
 destroy_job(c,j);
 if(refresh)(void)c->config.wsi_refresh(c->config.user);
 return leave(c,VSP_OK);
 
}
int vsp_destroy(VspContext**out){if(!out||!*out)return VSP_INVALID;
 VspContext*c=*out;
 if(!enter(c))return VSP_OWNER;
 if(c->job)return leave(c,VSP_BUSY);
 destroy_context(c);
 *out=NULL;
 return VSP_OK;
 }
