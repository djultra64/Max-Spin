/* Isolated real Vulkan provider, own work djultra64. No production wiring. */
#include "visual_vk_snapshot.h"
#include <stdlib.h>
#include <string.h>
#define VKS_JOBS 128u
typedef struct VksFence {struct VksFence*next;VkFence fence;unsigned references;} VksFence;
struct VksLease {
 struct VksLease *next;VksContext *context;VksSource source;
 VkImage image;VkDeviceMemory memory;VkImageLayout layout;VkDeviceSize charge;
 unsigned references,pending,copy_ready,aliases,consumer_count;
 uint64_t identities[16];VksFence*consumers[VKS_MAX_FENCES];
};
typedef struct VksJob {
 struct VksJob*next;VksLease*owner;VkCommandPool pool;VkCommandBuffer command;
 VkFence fence;VkBuffer buffer;VkDeviceMemory memory;VkDeviceSize charge;
 int submitted,completed,is_copy;
} VksJob;
struct VksContext {
 VkPhysicalDevice physical;VkDevice device;VkQueue queue;uint32_t family;
 VkPhysicalDeviceMemoryProperties memory;VksAccess access;
 uint64_t device_generation,max_bytes,bytes;unsigned objects,jobs,busy;
 VksLease*leases;VksJob*pending;VksFence*fences;
};
static int same(VfpGeneration a,VfpGeneration b){return a.scene==b.scene&&a.view==b.view;}
static int live_generation(VksContext*c,VfpGeneration*g){uint64_t device=0;return c&&c->access.generation(c->access.user,g,&device)&&device==c->device_generation;}
static int memory_type(VksContext*c,uint32_t mask,VkMemoryPropertyFlags flags,uint32_t*out){
 for(uint32_t i=0;i<c->memory.memoryTypeCount;i++)if((mask&(1u<<i))&&(c->memory.memoryTypes[i].propertyFlags&flags)==flags){*out=i;return 1;}
 return 0;
}
static int room(VksContext*c,VkDeviceSize bytes){return bytes&&bytes<=c->max_bytes&&c->bytes<=c->max_bytes-bytes;}
static void image_destroy(VksContext*c,VksLease*l){if(l->image)vkDestroyImage(c->device,l->image,NULL);if(l->memory)vkFreeMemory(c->device,l->memory,NULL);c->bytes-=l->charge;free(l);}
static void job_destroy(VksContext*c,VksJob*j){if(j->fence)vkDestroyFence(c->device,j->fence,NULL);if(j->pool)vkDestroyCommandPool(c->device,j->pool,NULL);if(j->buffer)vkDestroyBuffer(c->device,j->buffer,NULL);if(j->memory)vkFreeMemory(c->device,j->memory,NULL);c->bytes-=j->charge;free(j);}
static VksJob*job_create(VksContext*c,VksLease*owner){
 if(c->jobs>=VKS_JOBS)return NULL;
 VksJob*j=calloc(1,sizeof*j);if(!j)return NULL;j->owner=owner;
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=c->family};
 VkCommandPool pool=VK_NULL_HANDLE;if(vkCreateCommandPool(c->device,&pi,NULL,&pool)!=VK_SUCCESS)goto fail;j->pool=pool;
 VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=j->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
 VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(c->device,&ai,&command)!=VK_SUCCESS)goto fail;j->command=command;
 VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;if(vkCreateFence(c->device,&fi,NULL,&fence)!=VK_SUCCESS)goto fail;j->fence=fence;
 VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};if(vkBeginCommandBuffer(j->command,&bi)!=VK_SUCCESS)goto fail;
 return j;
fail:job_destroy(c,j);return NULL;
}
static void barrier(VkCommandBuffer cmd,VkImage image,VkImageAspectFlags aspects,uint32_t mips,uint32_t layers,VkImageLayout old,VkImageLayout next){
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={aspects,0,mips,0,layers}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
static void unlink_job(VksContext*c,VksJob*j){VksJob**p=&c->pending;while(*p&&*p!=j)p=&(*p)->next;if(*p){*p=j->next;c->jobs--;j->owner->pending--;if(j->is_copy)j->owner->copy_ready=1;}}
/* A timeout/error never frees an in-flight command or staging allocation. */
static int submit_wait(VksContext*c,VksJob*j){
 if(vkEndCommandBuffer(j->command)!=VK_SUCCESS)return 0;
 VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&j->command};
 VkResult submitted=vkQueueSubmit(c->queue,1,&si,j->fence);
 if(submitted!=VK_SUCCESS&&submitted!=VK_ERROR_DEVICE_LOST)return 0;
 /* DEVICE_LOST is equivalent to SUCCESS for pending/in-use accounting.
  * Do not destroy command pools/images based on an ambiguous submission. */
 j->submitted=1;j->next=c->pending;c->pending=j;c->jobs++;j->owner->pending++;
 if(submitted==VK_ERROR_DEVICE_LOST)return 0;
 if(vkWaitForFences(c->device,1,&j->fence,VK_TRUE,1000000000ull)!=VK_SUCCESS)return 0;
 unlink_job(c,j);j->completed=1;return 1;
}
static VkImageAspectFlags format_aspects(VkFormat format){
 switch(format){
 case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_R5G6B5_UNORM_PACK16:return VK_IMAGE_ASPECT_COLOR_BIT;
 case VK_FORMAT_D16_UNORM:case VK_FORMAT_D32_SFLOAT:return VK_IMAGE_ASPECT_DEPTH_BIT;
 case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
 default:return 0;
 }
}
static int source_valid(const VfpResource*r,const VksSource*s,VksContext*c){
 uint32_t max_mips=1,n=s->width>s->height?s->width:s->height;while(n>1){n>>=1;max_mips++;}
 VkImageAspectFlags allowed=VK_IMAGE_ASPECT_COLOR_BIT|VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
 return s->aspects==format_aspects(s->format)&&s->image&&s->storage_identity&&s->content_version==r->version&&s->device_generation==c->device_generation&&s->format!=VK_FORMAT_UNDEFINED&&s->layout!=VK_IMAGE_LAYOUT_UNDEFINED&&s->layout!=VK_IMAGE_LAYOUT_PREINITIALIZED&&(s->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&s->width&&s->height&&s->width<=16384&&s->height<=16384&&s->mips&&s->mips<=max_mips&&s->layers&&(s->layers==1||s->layers==6)&&s->samples==VK_SAMPLE_COUNT_1_BIT&&s->aspects&&!(s->aspects&~allowed)&&!((s->aspects&VK_IMAGE_ASPECT_COLOR_BIT)&&(s->aspects!=VK_IMAGE_ASPECT_COLOR_BIT))&&!(s->flags&~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)&&(!(s->flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)||(s->layers==6&&s->width==s->height));
}
static int key_equal(const VksSource*a,const VksSource*b){return a->image==b->image&&a->storage_identity==b->storage_identity&&a->content_version==b->content_version&&a->device_generation==b->device_generation&&a->format==b->format&&a->aspects==b->aspects&&a->width==b->width&&a->height==b->height&&a->mips==b->mips&&a->layers==b->layers;}
static int record_alias(VksLease*l,uint64_t id){for(unsigned i=0;i<l->aliases;i++)if(l->identities[i]==id)return 1;if(l->aliases==16)return 0;l->identities[l->aliases++]=id;return 1;}
static int provider_generation(void*u,VfpGeneration*g){return live_generation(u,g);}
static int retain(void*u,const VfpResource*r,void**out){
 VksContext*c=u;VksSource source={0};VksLease*l=NULL;VksJob*j=NULL;int ok=0;VfpGeneration before,after;
 if(!out)return 0;
 *out=NULL;if(!c||!r||!r->identity||!r->version||c->busy||!live_generation(c,&before))return 0;
 c->busy=1;if(!c->access.lock(c->access.user)){c->busy=0;return 0;}
 if(!c->access.resolve(c->access.user,r,&source)||!source_valid(r,&source,c))goto done;
 for(l=c->leases;l;l=l->next)if(l->copy_ready&&key_equal(&l->source,&source)){
  if(l->references==UINT32_MAX||!record_alias(l,r->identity))goto done;
  l->references++;*out=l;ok=1;goto done;
 }
 if(c->objects==VKS_MAX_OBJECTS)goto done;
 l=calloc(1,sizeof*l);if(!l)goto done;l->context=c;l->source=source;l->references=1;record_alias(l,r->identity);
 VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=source.flags,.imageType=VK_IMAGE_TYPE_2D,.format=source.format,.extent={source.width,source.height,1},.mipLevels=source.mips,.arrayLayers=source.layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 VkImage image=VK_NULL_HANDLE;if(vkCreateImage(c->device,&ii,NULL,&image)!=VK_SUCCESS)goto unlinked;l->image=image;
 VkMemoryRequirements mr;vkGetImageMemoryRequirements(c->device,l->image,&mr);uint32_t type;
 if(!room(c,mr.size)||!memory_type(c,mr.memoryTypeBits,0,&type))goto unlinked;
 VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=type};
 VkDeviceMemory memory=VK_NULL_HANDLE;if(vkAllocateMemory(c->device,&ma,NULL,&memory)!=VK_SUCCESS)goto unlinked;l->memory=memory;
 l->charge=mr.size;c->bytes+=mr.size;
 if(vkBindImageMemory(c->device,l->image,l->memory,0)!=VK_SUCCESS)goto unlinked;
 j=job_create(c,l);if(!j)goto unlinked;j->is_copy=1;
 barrier(j->command,source.image,source.aspects,source.mips,source.layers,source.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 barrier(j->command,l->image,source.aspects,source.mips,source.layers,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 for(uint32_t mip=0;mip<source.mips;mip++)for(unsigned bit=0;bit<3;bit++)if(source.aspects&(1u<<bit)){
  uint32_t w=source.width>>mip,h=source.height>>mip;VkImageCopy region={.srcSubresource={1u<<bit,mip,0,source.layers},.dstSubresource={1u<<bit,mip,0,source.layers},.extent={w?w:1,h?h:1,1}};
  vkCmdCopyImage(j->command,source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,l->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 }
 l->layout=source.aspects&VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
 barrier(j->command,l->image,source.aspects,source.mips,source.layers,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,l->layout);
 barrier(j->command,source.image,source.aspects,source.mips,source.layers,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,source.layout);
 if(!submit_wait(c,j)){
  if(j->submitted){l->references=0;l->next=c->leases;c->leases=l;c->objects++;j=NULL;l=NULL;goto done;}
  goto unlinked;
 }
 job_destroy(c,j);j=NULL;
 if(!live_generation(c,&after)||!same(before,after))goto unlinked;
 l->next=c->leases;c->leases=l;c->objects++;*out=l;ok=1;goto done;
unlinked:
 if(j)job_destroy(c,j);
 if(l)image_destroy(c,l);
done:
 c->access.unlock(c->access.user);c->busy=0;return ok;
}
static int validate(void*u,const VfpResource*r,void*lease){VksContext*c=u;VksLease*l=lease;VfpGeneration g;if(!c||!l||l->context!=c||!l->references||!l->copy_ready||!r||r->version!=l->source.content_version||!live_generation(c,&g))return 0;for(unsigned i=0;i<l->aliases;i++)if(l->identities[i]==r->identity)return 1;return 0;}
static void release(void*u,const VfpResource*r,void*lease){(void)r;VksContext*c=u;VksLease*l=lease;if(c&&l&&l->context==c&&l->references)l->references--;}
VksContext*vks_create(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,const VksAccess*access,uint64_t maximum){
 if(!physical||!device||!queue||!access||!access->lock||!access->unlock||!access->resolve||!access->generation||!maximum||maximum>VKS_MAX_BYTES)return NULL;
 VfpGeneration g;uint64_t generation;if(!access->generation(access->user,&g,&generation)||!generation)return NULL;
 VksContext*c=calloc(1,sizeof*c);if(!c)return NULL;c->physical=physical;c->device=device;c->queue=queue;c->family=family;c->access=*access;c->device_generation=generation;c->max_bytes=maximum;vkGetPhysicalDeviceMemoryProperties(physical,&c->memory);if(!c->memory.memoryTypeCount||c->memory.memoryTypeCount>32){free(c);return NULL;}return c;
}
VfpProvider vks_provider(VksContext*c){return c?(VfpProvider){c,provider_generation,retain,validate,release}:(VfpProvider){0};}
int vks_resource(VksContext*c,const VfpResource*r,void**out){if(!out)return 0;*out=NULL;if(!c||!r)return 0;for(VksLease*l=c->leases;l;l=l->next)if(validate(c,r,l)){*out=l;return 1;}return 0;}
int vks_image(VksContext*c,void*lease,VkImage*image,VkFormat*format,uint32_t*w,uint32_t*h){VksLease*l=lease;VfpGeneration g;if(!c||!l||l->context!=c||!l->references||!l->copy_ready||!live_generation(c,&g))return 0;if(image)*image=l->image;if(format)*format=l->source.format;if(w)*w=l->source.width;if(h)*h=l->source.height;return 1;}
int vks_add_consumer_fence(VksContext*c,void*lease,VkFence fence){
 VksLease*l=lease;if(!c||!l||l->context!=c||!l->references||c->busy||!fence||l->consumer_count==VKS_MAX_FENCES)return 0;
 for(unsigned i=0;i<l->consumer_count;i++)if(l->consumers[i]->fence==fence)return 0;
 VksFence*owned=c->fences;while(owned&&owned->fence!=fence)owned=owned->next;
 if(!owned){owned=calloc(1,sizeof*owned);if(!owned)return 0;owned->fence=fence;owned->next=c->fences;c->fences=owned;}
 owned->references++;l->consumers[l->consumer_count++]=owned;return 1;
}
static void fence_release(VksContext*c,VksFence*f){
 if(--f->references)return;
 VksFence**p=&c->fences;while(*p&&*p!=f)p=&(*p)->next;
 if(*p)*p=f->next;
 vkDestroyFence(c->device,f->fence,NULL);free(f);
}

unsigned vks_collect(VksContext*c){
 if(!c||c->busy)return 0;
 c->busy=1;unsigned retired=0;
 VksJob*j=c->pending;while(j){VksJob*next=j->next;if(vkGetFenceStatus(c->device,j->fence)==VK_SUCCESS){unlink_job(c,j);job_destroy(c,j);}j=next;}
 VksLease**p=&c->leases;while(*p){VksLease*l=*p;unsigned i=0;while(i<l->consumer_count){if(vkGetFenceStatus(c->device,l->consumers[i]->fence)==VK_SUCCESS){fence_release(c,l->consumers[i]);l->consumers[i]=l->consumers[--l->consumer_count];}else i++;}
  if(!l->references&&!l->pending&&!l->consumer_count){*p=l->next;c->objects--;image_destroy(c,l);retired++;}else p=&l->next;
 }
 c->busy=0;return retired;
}
int vks_destroy(VksContext**context){if(!context||!*context)return 1;VksContext*c=*context;vks_collect(c);if(c->leases||c->pending||c->fences||c->busy)return 0;free(c);*context=NULL;return 1;}
uint64_t vks_charged_bytes(const VksContext*c){return c?c->bytes:0;}
static unsigned pixel_size(VkFormat f){switch(f){case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_D32_SFLOAT:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return 4;case VK_FORMAT_R5G6B5_UNORM_PACK16:case VK_FORMAT_D16_UNORM:return 2;default:return 0;}}
int vks_readback(VksContext*c,void*lease,uint32_t mip,uint32_t layer,void*out,size_t bytes){
 VksLease*l=lease;VksJob*j=NULL;int ok=0;VfpGeneration g;
 if(!c||!l||l->context!=c||!l->references||!l->copy_ready||c->busy||!out||!live_generation(c,&g)||mip>=l->source.mips||layer>=l->source.layers)return 0;
 unsigned bpp=pixel_size(l->source.format);uint32_t w=l->source.width>>mip,h=l->source.height>>mip;w=w?w:1;h=h?h:1;if(!bpp||bytes!=(uint64_t)w*h*bpp)return 0;
 c->busy=1;if(!c->access.lock(c->access.user)){c->busy=0;return 0;}
 j=job_create(c,l);if(!j)goto done;
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer=VK_NULL_HANDLE;if(vkCreateBuffer(c->device,&bi,NULL,&buffer)!=VK_SUCCESS)goto done;j->buffer=buffer;
 VkMemoryRequirements mr;vkGetBufferMemoryRequirements(c->device,j->buffer,&mr);uint32_t type;
 if(!room(c,mr.size)||!memory_type(c,mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&type))goto done;
 VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=type};VkDeviceMemory memory=VK_NULL_HANDLE;if(vkAllocateMemory(c->device,&ma,NULL,&memory)!=VK_SUCCESS)goto done;j->memory=memory;j->charge=mr.size;c->bytes+=mr.size;
 if(vkBindBufferMemory(c->device,j->buffer,j->memory,0)!=VK_SUCCESS)goto done;
 barrier(j->command,l->image,l->source.aspects,l->source.mips,l->source.layers,l->layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 VkImageAspectFlags aspect=l->source.aspects&VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
 VkBufferImageCopy region={.imageSubresource={aspect,mip,layer,1},.imageExtent={w,h,1}};vkCmdCopyImageToBuffer(j->command,l->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,j->buffer,1,&region);
 VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
 vkCmdPipelineBarrier(j->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
 barrier(j->command,l->image,l->source.aspects,l->source.mips,l->source.layers,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,l->layout);
 if(!submit_wait(c,j))goto done;
 void*map=NULL;if(vkMapMemory(c->device,j->memory,0,bytes,0,&map)!=VK_SUCCESS)goto done;memcpy(out,map,bytes);vkUnmapMemory(c->device,j->memory);ok=1;
done:
 if(j&&(!j->submitted||j->completed))job_destroy(c,j);
 c->access.unlock(c->access.user);c->busy=0;return ok;
}

#include "visual_scene_retained_describe.inc"
