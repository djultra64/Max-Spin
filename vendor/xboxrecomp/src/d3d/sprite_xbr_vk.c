/* Three/six-pass Vulkan sprite filter proposal. Own work djultra64. */
#include "sprite_xbr_vk.h"
#include <stdlib.h>
#include <string.h>
typedef struct {VkImage image;VkDeviceMemory memory;VkImageView view;VkDeviceSize charge;} Image;
typedef struct {
 SxVkKey key;Image images[6];unsigned count,ready,submitted;
 VkCommandPool pool;VkCommandBuffer command;VkDescriptorPool descriptors;
 VkFence fence;VkImageView source_view;SxVkSnapshot snapshot;
} Entry;
struct SxVkContext {
 VkPhysicalDevice physical;VkDevice device;VkQueue queue;uint32_t family;
 uint64_t generation,bytes;SxVkAccess access;VkPhysicalDeviceMemoryProperties memory;
 VkDescriptorSetLayout set_layout;VkPipelineLayout layout;VkPipeline pipes[4];
 VkSampler nearest;unsigned busy,count;Entry entries[SXVK_CACHE];
};
static int owned(SxVkContext*c){return c&&c->access.owner(c->access.user);}
static uint64_t external(SxVkContext*c){return c->access.external_bytes?c->access.external_bytes(c->access.user):0;}
static int room(SxVkContext*c,uint64_t n){uint64_t held=external(c);return n&&n<=SXVK_BUDGET&&held<=SXVK_BUDGET-n&&c->bytes<=SXVK_BUDGET-n-held;}
static int same(const SxVkKey*a,const SxVkKey*b){return a->allocation==b->allocation&&a->version==b->version&&a->device_generation==b->device_generation&&a->profile==b->profile&&a->width==b->width&&a->height==b->height&&a->guest_format==b->guest_format&&a->factor==b->factor;}
static int valid(SxVkContext*c,const SxVkKey*k){return k&&k->allocation&&k->version&&k->device_generation==c->generation&&k->profile>=1&&k->profile<=3&&k->guest_format==0x0e&&((k->profile==1&&k->width==16&&k->height==16)||(k->profile==2&&k->width==64&&k->height==16)||(k->profile==3&&k->width==128&&k->height==32))&&(k->factor==2||k->factor==4);}
static void image_drop(SxVkContext*c,Image*i){if(i->view)vkDestroyImageView(c->device,i->view,NULL);if(i->image)vkDestroyImage(c->device,i->image,NULL);if(i->memory)vkFreeMemory(c->device,i->memory,NULL);c->bytes-=i->charge;memset(i,0,sizeof*i);}
static void entry_drop(SxVkContext*c,Entry*e){
 if(e->fence)vkDestroyFence(c->device,e->fence,NULL);
 if(e->pool)vkDestroyCommandPool(c->device,e->pool,NULL);
 if(e->descriptors)vkDestroyDescriptorPool(c->device,e->descriptors,NULL);
 if(e->source_view)vkDestroyImageView(c->device,e->source_view,NULL);
 for(unsigned i=0;i<e->count;i++)image_drop(c,&e->images[i]);
 if(e->snapshot.lease){uint64_t charge=e->snapshot.charged_bytes;c->access.release(c->access.user,&e->snapshot);c->bytes-=charge;}
 memset(e,0,sizeof*e);
}
static int image_make(SxVkContext*c,Image*i,uint32_t w,uint32_t h,int final){
 VkFormat format=final?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R32G32B32A32_SFLOAT;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 VkImage image=VK_NULL_HANDLE;if(vkCreateImage(c->device,&ci,NULL,&image)!=VK_SUCCESS)return 0;i->image=image;
 VkMemoryRequirements r;vkGetImageMemoryRequirements(c->device,i->image,&r);uint32_t type=UINT32_MAX;
 for(uint32_t n=0;n<c->memory.memoryTypeCount;n++)if((r.memoryTypeBits&(1u<<n))&&(c->memory.memoryTypes[n].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=n;break;}
 if(type==UINT32_MAX||!room(c,r.size))goto fail;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,.memoryTypeIndex=type};VkDeviceMemory memory=VK_NULL_HANDLE;
 if(vkAllocateMemory(c->device,&ai,NULL,&memory)!=VK_SUCCESS)goto fail;i->memory=memory;i->charge=r.size;c->bytes+=r.size;
 if(vkBindImageMemory(c->device,i->image,i->memory,0)!=VK_SUCCESS)goto fail;
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=i->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};VkImageView view=VK_NULL_HANDLE;
 if(vkCreateImageView(c->device,&vi,NULL,&view)!=VK_SUCCESS)goto fail;i->view=view;return 1;
fail:image_drop(c,i);return 0;
}
static void barrier(VkCommandBuffer cmd,Image*i,int start){
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=start?0:VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=start?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT,.oldLayout=start?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL,.newLayout=start?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=i->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 vkCmdPipelineBarrier(cmd,start?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,start?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);
}
static int record(SxVkContext*c,Entry*e){
 unsigned passes=e->key.factor==4?6:3;uint32_t w=e->key.width,h=e->key.height;
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=c->family};VkCommandPool pool=VK_NULL_HANDLE;
 if(vkCreateCommandPool(c->device,&pi,NULL,&pool)!=VK_SUCCESS)return 0;e->pool=pool;
 VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=e->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cmd=VK_NULL_HANDLE;
 if(vkAllocateCommandBuffers(c->device,&ai,&cmd)!=VK_SUCCESS)return 0;e->command=cmd;
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,passes*2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,passes}};
 VkDescriptorPoolCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=passes,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool descriptors=VK_NULL_HANDLE;
 if(vkCreateDescriptorPool(c->device,&di,NULL,&descriptors)!=VK_SUCCESS)return 0;e->descriptors=descriptors;
 VkDescriptorSetLayout layouts[6];for(unsigned i=0;i<passes;i++)layouts[i]=c->set_layout;VkDescriptorSet sets[6];
 VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=e->descriptors,.descriptorSetCount=passes,.pSetLayouts=layouts};if(vkAllocateDescriptorSets(c->device,&da,sets)!=VK_SUCCESS)return 0;
 VkImageView source_view=VK_NULL_HANDLE;VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=e->snapshot.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=e->snapshot.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 if(vkCreateImageView(c->device,&vi,NULL,&source_view)!=VK_SUCCESS)return 0;
 /* Source view survives the complete submitted command. */
 VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
 if(vkBeginCommandBuffer(cmd,&bi)!=VK_SUCCESS){vkDestroyImageView(c->device,source_view,NULL);return 0;}
 e->source_view=source_view;
 for(unsigned i=0;i<passes;i++){
  unsigned phase=i%3,final=i+1==passes;if(phase==1){w*=2;h*=2;}
  if(!image_make(c,&e->images[i],w,h,final))return 0;e->count=i+1;
  VkImageView orig=i<3?source_view:e->images[2].view,input=phase==0?orig:e->images[i-1].view;
  VkDescriptorImageInfo infos[3]={{c->nearest,orig,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{c->nearest,input,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,e->images[i].view,VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[3];memset(writes,0,sizeof writes);for(unsigned b=0;b<3;b++)writes[b]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i],.dstBinding=b,.descriptorCount=1,.descriptorType=b==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&infos[b]};vkUpdateDescriptorSets(c->device,3,writes,0,NULL);
  barrier(cmd,&e->images[i],1);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,c->pipes[final?3:phase]);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,c->layout,0,1,&sets[i],0,NULL);
  uint32_t pc[4]={w,h,i>=3,0};vkCmdPushConstants(cmd,c->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,pc);vkCmdDispatch(cmd,(w+7)/8,(h+7)/8,1);barrier(cmd,&e->images[i],0);
 }
 VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;if(vkCreateFence(c->device,&fi,NULL,&fence)!=VK_SUCCESS)return 0;e->fence=fence;
 return vkEndCommandBuffer(cmd)==VK_SUCCESS;
}
static void objects_drop(SxVkContext*c){
 for(unsigned i=0;i<4;i++)if(c->pipes[i])vkDestroyPipeline(c->device,c->pipes[i],NULL);
 if(c->nearest)vkDestroySampler(c->device,c->nearest,NULL);
 if(c->layout)vkDestroyPipelineLayout(c->device,c->layout,NULL);
 if(c->set_layout)vkDestroyDescriptorSetLayout(c->device,c->set_layout,NULL);
}
int sxvk_supported(VkPhysicalDevice physical,uint32_t family){
 if(!physical)return 0;
 VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(physical,&prop);
 if(prop.limits.maxImageDimension2D<1024||prop.limits.maxPushConstantsSize<16||prop.limits.maxComputeWorkGroupInvocations<64||prop.limits.maxComputeWorkGroupSize[0]<8||prop.limits.maxComputeWorkGroupSize[1]<8||prop.limits.maxComputeWorkGroupCount[0]<128||prop.limits.maxComputeWorkGroupCount[1]<128)return 0;
 uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&queues,NULL);if(family>=queues||queues>256)return 0;VkQueueFamilyProperties qp[256];vkGetPhysicalDeviceQueueFamilyProperties(physical,&queues,qp);if((qp[family].queueFlags&(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))!=(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))return 0;
 const VkFormat formats[2]={VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM};
 for(unsigned i=0;i<2;i++){VkFormatProperties p;vkGetPhysicalDeviceFormatProperties(physical,formats[i],&p);VkFormatFeatureFlags need=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;if(i==1)need|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;if((p.optimalTilingFeatures&need)!=need)return 0;}
 return 1;
}
SxVkContext*sxvk_create(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,uint64_t generation,const SxVkAccess*access,const uint32_t*float_spv,size_t float_bytes,const uint32_t*rgba_spv,size_t rgba_bytes){
 if(!physical||!device||!queue||!generation||!access||!access->owner||!access->acquire||!access->release||!access->owner(access->user)||!float_spv||!rgba_spv||float_bytes<20||rgba_bytes<20||float_bytes%4||rgba_bytes%4||float_bytes>1024*1024||rgba_bytes>1024*1024||float_spv[0]!=0x07230203||rgba_spv[0]!=0x07230203)return NULL;
 if(!sxvk_supported(physical,family))return NULL;
 SxVkContext*c=calloc(1,sizeof*c);if(!c)return NULL;c->physical=physical;c->device=device;c->queue=queue;c->family=family;c->generation=generation;c->access=*access;vkGetPhysicalDeviceMemoryProperties(physical,&c->memory);
 VkDescriptorSetLayoutBinding bs[3];memset(bs,0,sizeof bs);for(unsigned i=0;i<3;i++)bs[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bs};VkDescriptorSetLayout set=VK_NULL_HANDLE;if(vkCreateDescriptorSetLayout(device,&si,NULL,&set)!=VK_SUCCESS)goto fail;c->set_layout=set;
 VkPushConstantRange pc={VK_SHADER_STAGE_COMPUTE_BIT,0,16};VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&c->set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&pc};VkPipelineLayout layout=VK_NULL_HANDLE;
 if(vkCreatePipelineLayout(device,&li,NULL,&layout)!=VK_SUCCESS)goto fail;c->layout=layout;
 VkSamplerCreateInfo ni={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=0};VkSampler nearest=VK_NULL_HANDLE;
 if(vkCreateSampler(device,&ni,NULL,&nearest)!=VK_SUCCESS)goto fail;c->nearest=nearest;
 for(unsigned i=0;i<4;i++){
  VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=i==3?rgba_bytes:float_bytes,.pCode=i==3?rgba_spv:float_spv};VkShaderModule module=VK_NULL_HANDLE;if(vkCreateShaderModule(device,&mi,NULL,&module)!=VK_SUCCESS)goto fail;
  uint32_t phase=i==3?2:i;VkSpecializationMapEntry map={0,0,sizeof phase};VkSpecializationInfo specialization={1,&map,sizeof phase,&phase};
  VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main",.pSpecializationInfo=&specialization},.layout=c->layout};VkPipeline pipe=VK_NULL_HANDLE;
  VkResult r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&pipe);vkDestroyShaderModule(device,module,NULL);if(r!=VK_SUCCESS){if(pipe)vkDestroyPipeline(device,pipe,NULL);goto fail;}c->pipes[i]=pipe;
 }return c;
fail:objects_drop(c);free(c);return NULL;
}
void sxvk_collect(SxVkContext*c){if(!owned(c)||c->busy)return;c->busy=1;for(unsigned i=0;i<SXVK_CACHE;i++){Entry*e=&c->entries[i];if(e->submitted&&!e->ready&&vkGetFenceStatus(c->device,e->fence)==VK_SUCCESS)entry_drop(c,e);}c->busy=0;}
VkImageView sxvk_get(SxVkContext*c,const SxVkKey*k){
 if(!owned(c)||c->busy||!valid(c,k))return VK_NULL_HANDLE;
 sxvk_collect(c);Entry*e=NULL;for(unsigned i=0;i<SXVK_CACHE;i++){
  Entry*at=&c->entries[i];if(at->key.allocation&&same(&at->key,k))return at->ready?at->images[0].view:VK_NULL_HANDLE;
  if(!at->key.allocation&&!e)e=at;
 }if(!e)return VK_NULL_HANDLE;c->busy=1;e->key=*k;
 SxVkSnapshot snapshot={0};uint64_t held=external(c);if(held>SXVK_BUDGET-c->bytes)goto fail;uint64_t allowance=SXVK_BUDGET-c->bytes-held;
 if(!c->access.acquire(c->access.user,k,allowance,&snapshot))goto fail;
 if(!snapshot.lease)goto fail;
 /* Provider contract forbids charges beyond allowance. Reject wrong image
  * metadata before using it, but release its successful lease exactly once. */
 if(!snapshot.charged_bytes||snapshot.charged_bytes>allowance||!snapshot.image||snapshot.format!=VK_FORMAT_R8G8B8A8_UNORM||snapshot.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||snapshot.width!=k->width||snapshot.height!=k->height){c->access.release(c->access.user,&snapshot);goto fail;}
 e->snapshot=snapshot;c->bytes+=snapshot.charged_bytes;
 if(!record(c,e))goto fail;
 VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&e->command};VkResult r=vkQueueSubmit(c->queue,1,&si,e->fence);
 if(r!=VK_SUCCESS&&r!=VK_ERROR_DEVICE_LOST)goto fail;
 e->submitted=1;
 if(r==VK_ERROR_DEVICE_LOST||vkWaitForFences(c->device,1,&e->fence,VK_TRUE,1000000000ull)!=VK_SUCCESS){c->busy=0;return VK_NULL_HANDLE;}
 Image result=e->images[e->count-1];memset(&e->images[e->count-1],0,sizeof(Image));SxVkKey saved=e->key;
 entry_drop(c,e);e->key=saved;e->images[0]=result;e->count=1;e->ready=1;c->busy=0;return result.view;
fail:entry_drop(c,e);c->busy=0;return VK_NULL_HANDLE;
}
uint64_t sxvk_bytes(const SxVkContext*c){return c?c->bytes:0;}
int sxvk_destroy_idle(SxVkContext**ptr){
 if(!ptr||!*ptr)return 1;SxVkContext*c=*ptr;if(!owned(c)||c->busy)return 0;sxvk_collect(c);
 for(unsigned i=0;i<SXVK_CACHE;i++)if(c->entries[i].submitted&&!c->entries[i].ready)return 0;
 for(unsigned i=0;i<SXVK_CACHE;i++)entry_drop(c,&c->entries[i]);objects_drop(c);free(c);*ptr=NULL;return 1;
}
