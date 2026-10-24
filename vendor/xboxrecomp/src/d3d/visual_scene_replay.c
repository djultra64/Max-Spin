/* Fresh device-local replay. No original backend global or guest access.
 * Every Vulkan allocation/command/fence below is owned here. djultra64. */
#include "visual_scene_replay.h"
#include "visual_scene_recorder.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MAX_RES VSJ_MAX_RESOURCES
_Static_assert(sizeof(VkViewport)==6*sizeof(float),"Exact viewport codec ABI");
#define MAX_CMD VSJ_MAX_COMMANDS
#define NONE UINT32_MAX
/* Vulkan may leave failure outputs undefined. Never publish them to cleanup. */
#define ACQUIRE(type,destination,call) do{type acquired=VK_NULL_HANDLE;if((call)!=VK_SUCCESS)return 0;(destination)=acquired;}while(0)
typedef struct {
 VksSource source;
 VsjUse initial;
 uint64_t current;
 int fresh;
 
 VkImage image;
 VkDeviceMemory memory;
 VkImageView sampled,attachment;
 VkImageLayout layout;
 
} Resource;
 
typedef struct {
 const VsjCommand*command;
 uint32_t use[VSJ_MAX_USES],color,depth,texture[4];
 
 VsrDrawNative draw;
 D3D8SceneGpuDraw material;
 
 VkDeviceSize vertex,index,ps,vs,frame,upload;
 uint32_t vertices,indices,points;
 
 VkRenderPass pass;
 VkFramebuffer framebuffer;
 VkDescriptorSet descriptors;
 
} Command;
 
typedef struct {
 const VsjFrame*frame;
 Resource resource[MAX_RES];
 Command command[MAX_CMD];
 uint32_t resources,commands,final;
 
 VkCommandPool pool;
 VkCommandBuffer cmd;
 VkFence fence;
 VkDescriptorPool descriptors;
 
 VkBuffer buffer;
 VkDeviceMemory memory;
 unsigned char*mapped;
 
 VkDeviceSize packed,gpu_bytes;
 uint64_t ticket,consumers[8];
 int submitted,complete,lost;
 
} Job;
 
struct VreContext{VreConfig config;
 VkPhysicalDeviceMemoryProperties memory;
 VkDeviceSize alignment;
 uint64_t nonce,consumer_nonce;
 int busy;
 Job*job;
 };
 
static int enter(VreContext*c){if(!c||c->busy)return 0;
 c->busy=1;
 if(!c->config.owner(c->config.user)){c->busy=0;
 return 0;
 }return 1;
 }
static int leave(VreContext*c,int r){c->busy=0;
 return r;
 }
static int admitted(VreContext*c){return c->config.admitted(c->config.user,c->config.device_epoch);
 }
static const void*chunk(const VsjCommand*c,unsigned kind,size_t*bytes){
 const VsjOwnedChunk*found=NULL;
 if(c->chunk_count>VSJ_MAX_CHUNKS)return NULL;
 
 for(unsigned i=0;i<c->chunk_count;i++){const VsjOwnedChunk*x=&c->chunks[i];
 if(x->offset>c->payload_bytes||x->bytes>c->payload_bytes-x->offset)return NULL;
 if(x->encoding==kind){if(found)return NULL;
 found=x;
 }}
 if(!found||!c->payload)return NULL;
 *bytes=found->bytes;
 return c->payload+found->offset;
 
}
static int exact_chunk(const VsjCommand*c,unsigned kind,void*out,size_t n){size_t bytes;
 const void*p=chunk(c,kind,&bytes);
 if(!p||bytes!=n)return 0;
 memcpy(out,p,n);
 return 1;
 }
static int reserve(VreContext*c,Job*j,VkDeviceSize n,VkDeviceSize alignment,VkDeviceSize*out){
 if(!alignment||alignment>VRE_MAX_BYTES||j->packed>VRE_MAX_BYTES-(alignment-1))return 0;
 
 VkDeviceSize at=(j->packed+alignment-1)/alignment*alignment;
 
 if(at>c->config.cpu_budget-sizeof(Job)-sizeof(VreContext)||n>c->config.cpu_budget-sizeof(Job)-sizeof(VreContext)-at||at>UINT32_MAX||n>UINT32_MAX-at)return 0;
 
 *out=at;
 j->packed=at+n;
 return 1;
 
}
static int source_shape(const VksSource*s){
 VkImageAspectFlags expected;
 switch(s->format){
 case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_R5G6B5_UNORM_PACK16:expected=VK_IMAGE_ASPECT_COLOR_BIT;break;
 case VK_FORMAT_D16_UNORM:case VK_FORMAT_D32_SFLOAT:expected=VK_IMAGE_ASPECT_DEPTH_BIT;break;
 case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:expected=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;break;
 default:return 0;
 }
 uint32_t mips=1,n=s->width>s->height?s->width:s->height;while(n>1){n>>=1;mips++;}
 return s->aspects==expected&&s->mips<=mips;
}
static int metadata(VreContext*c,const VsjFrame*f,const VsjDependency*d,VksSource*out){
 VsjUse u;
 void*lease=NULL;
 
 return d->retained_index!=NONE&&vsj_retained(f,d->retained_index,&u,&lease)&&
 u.resource.identity==d->before.resource.identity&&u.resource.version==d->before.resource.version&&u.guest_epoch==d->before.guest_epoch&&
 c->config.describe(c->config.user,&u,lease,out)&&out->image&&out->device_generation==c->config.device_epoch&&out->content_version==u.resource.version&&
 source_shape(out)&&out->width&&out->height&&out->width<=16384&&out->height<=16384&&out->mips&&out->mips<=15&&(out->layers==1||out->layers==6)&&out->samples==VK_SAMPLE_COUNT_1_BIT&&
 (out->layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||out->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL||out->layout==VK_IMAGE_LAYOUT_GENERAL)&&(out->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&(out->usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&
 !(out->flags&~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)&&((out->layers==1&&!(out->flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))||(out->layers==6&&(out->flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)&&out->width==out->height))&&
 (out->aspects==VK_IMAGE_ASPECT_COLOR_BIT||out->aspects==VK_IMAGE_ASPECT_DEPTH_BIT||out->aspects==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT));
 
}
static int view_ok(const D3D8SceneViewState*v){
 if(v->version!=1||v->bytes!=sizeof*v||!v->logical[0]||!v->logical[1]||!v->physical[0]||!v->physical[1]||v->physical[0]>16384||v->physical[1]>16384||v->depth_only>1)return 0;
 
 for(unsigned i=0;i<6;i++)if(!isfinite(v->viewport[i]))return 0;
 for(unsigned i=0;i<2;i++)if(!isfinite(v->canvas_scale[i])||!isfinite(v->placement[i]))return 0;
 
 return v->scissor_xy[0]>=0&&v->scissor_xy[1]>=0&&(uint64_t)v->scissor_xy[0]+v->scissor_wh[0]<=v->physical[0]&&(uint64_t)v->scissor_xy[1]+v->scissor_wh[1]<=v->physical[1];
 
}
static int targets(Job*j,Command*p,const D3D8SceneViewState*v){
 if(!view_ok(v))return 0;
 
 uint32_t refs[2]={p->color,p->depth};
 if((v->depth_only&&p->color!=NONE)||(!v->depth_only&&p->color==NONE)||refs[0]==refs[1])return 0;
 
 for(unsigned i=0;i<2;i++)if(refs[i]!=NONE){Resource*r=&j->resource[refs[i]];
 if(!r->fresh||r->source.width!=v->physical[0]||r->source.height!=v->physical[1]||r->source.layers!=1||((i==0)!=(r->source.aspects==VK_IMAGE_ASPECT_COLOR_BIT)))return 0;
 }
 return p->color!=NONE||p->depth!=NONE;
 
}
static int preflight(VreContext*c,Job*j,const VsjFrame*f){
 const VsjStamp*stamp=vsj_stamp(f);
 if(!stamp||stamp->device_epoch!=c->config.device_epoch||!vsj_validate(f))return 0;
 
 j->frame=f;
 j->commands=vsj_command_count(f);
 j->final=NONE;
 if(!j->commands||j->commands>MAX_CMD)return 0;
 
 for(unsigned k=0;k<j->commands;k++){
  Command*p=&j->command[k];
 const VsjCommand*s=vsj_command(f,k);
 p->command=s;
 p->color=p->depth=NONE;
 for(unsigned i=0;i<4;i++)p->texture[i]=NONE;
 
  if(!s||s->serial!=k+1||s->use_count>VSJ_MAX_USES)return 0;
 
  for(unsigned i=0;i<s->use_count;i++){
   const VsjDependency*d=vsj_dependency(f,s->use_indices[i]);
 if(!d||d->before.guest_epoch!=c->config.device_epoch)return 0;
 
   unsigned at=0;
 for(;at<j->resources;at++)if(j->resource[at].initial.resource.identity==d->before.resource.identity)break;
 
   if(at==j->resources){if(at==MAX_RES)return 0;
 Resource*r=&j->resource[at];
 if(!metadata(c,f,d,&r->source))return 0;
 r->initial=d->before;
 r->current=d->before.resource.version;
 j->resources++;
 }
   Resource*r=&j->resource[at];
 if(r->current!=d->before.resource.version||r->initial.resource.format!=d->before.resource.format||r->initial.resource.width!=d->before.resource.width||r->initial.resource.height!=d->before.resource.height||r->initial.resource.pitch!=d->before.resource.pitch)return 0;
 
   p->use[i]=at;
 unsigned role=d->before.resource.role;
 if(role>=6)return 0;
 
   if(d->before.access==VSJ_TARGET){r->fresh=1;
 if(d->after.resource.version<=r->current)return 0;
 r->current=d->after.resource.version;
 if(role==VFP_COLOR){if(p->color!=NONE)return 0;p->color=at;}
 else if(role==VFP_DEPTH){if(p->depth!=NONE)return 0;p->depth=at;}
 }
   else{if(d->before.access!=VSJ_INPUT||d->after.resource.version!=r->current)return 0;
 if(role<2)return 0;
 if(p->texture[role-2]!=NONE)return 0;
 p->texture[role-2]=at;
 }
  }
  if(s->kind==VSJ_PROGRAM_DRAW||s->kind==VSJ_UP_DRAW){
   if(!exact_chunk(s,VSJ_CHUNK_NATIVE_ARGS,&p->draw,sizeof p->draw)||p->draw.version!=VSR_VERSION||p->draw.kind!=s->kind||!targets(j,p,&p->draw.view)||
    !exact_chunk(s,VSJ_CHUNK_MATERIAL,&p->material,sizeof p->material)||!c->config.material_valid(c->config.user,&p->material)||p->material.device_epoch!=c->config.device_epoch)return 0;
 
   D3D8SceneGpuDraw*m=&p->material;
   if(m->version!=D3D8_SCENE_GPU_DRAW_VERSION||m->bytes!=sizeof*m||!m->ps_bytes||m->ps_bytes>D3D8_SCENE_PS_MAX)return 0;
 
   if(m->color_format!=(p->color==NONE?VK_FORMAT_UNDEFINED:j->resource[p->color].source.format)||m->has_depth!=(p->depth!=NONE)||(p->depth!=NONE&&m->depth_format!=j->resource[p->depth].source.format))return 0;
 
   for(unsigned i=0;i<4;i++){if(p->texture[i]==NONE)return 0;
 Resource*t=&j->resource[p->texture[i]];
 if(((m->cube_mask>>i)&1u)!=(t->source.layers==6))return 0;
 }
   size_t n;
 const void*g=chunk(s,VSJ_CHUNK_GEOMETRY,&n);
 if(!g||!p->draw.vertex_count)return 0;
 
   if(s->kind==VSJ_PROGRAM_DRAW){
    if(p->draw.geometry_encoding!=VSR_GEOMETRY_PROGRAM_PACKED)return 0;
    unsigned per=0;
 for(unsigned i=0;i<16;i++)per+=(p->draw.used>>i)&1u;
 if(!per)per=1;
 
    if(p->draw.camera.encoding!=VFP_CAMERA_BAKED_NV2A||(p->draw.used&0xffff0000u)||p->draw.vertex_stride!=per*16||m->layout!=per||n!=(uint64_t)p->draw.vertex_count*per*16||!p->draw.index_count||!p->draw.program_revision||p->draw.program_start>=136)return 0;
 
    size_t ni,np,nc;
 const uint16_t*indices=chunk(s,VSJ_CHUNK_INDICES,&ni);
 const void*program=chunk(s,VSJ_CHUNK_PROGRAM,&np);
 const void*constants=chunk(s,VSJ_CHUNK_CONSTANTS,&nc);
 
    if(!indices||ni!=(uint64_t)p->draw.index_count*2||!program||np!=136*16||!constants||nc!=192*16)return 0;
 
    p->points=p->draw.primitive==1;
    if(p->draw.emitted_vertex_stride!=p->draw.vertex_stride||p->draw.emitted_vertex_count!=(p->points?p->draw.index_count:p->draw.vertex_count))return 0;
 
    if(p->points){if(!m->instanced||m->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)return 0;
 p->vertices=p->draw.index_count;
 }
    else{if(p->draw.primitive!=5||m->instanced||m->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)return 0;
 for(unsigned i=0;i<p->draw.index_count;i++){uint16_t index;
 memcpy(&index,(const unsigned char*)indices+i*2,2);
 if(index>=p->draw.vertex_count)return 0;
 }p->vertices=p->draw.vertex_count;
 p->indices=p->draw.index_count;
 }
   }else{
#ifdef VSR_GEOMETRY_FIXED_GPU32
    if(p->draw.geometry_encoding!=VSR_GEOMETRY_FIXED_GPU32||p->draw.emitted_vertex_count!=p->draw.vertex_count||p->draw.emitted_vertex_stride!=32||p->draw.vertex_stride!=32||m->layout||m->instanced||n!=(uint64_t)p->draw.vertex_count*32)return 0;
 
    uint64_t count=p->draw.primitive_count;
    static const VkPrimitiveTopology topologies[7]={0,VK_PRIMITIVE_TOPOLOGY_POINT_LIST,VK_PRIMITIVE_TOPOLOGY_LINE_LIST,VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    if(!count||p->draw.primitive<1||p->draw.primitive>6)return 0;
    if(p->draw.primitive==2)count*=2;else if(p->draw.primitive==3)count++;else if(p->draw.primitive==4||p->draw.primitive==6)count*=3;else if(p->draw.primitive==5)count+=2;
    if(count!=p->draw.vertex_count||m->topology!=topologies[p->draw.primitive])return 0;
    p->vertices=p->draw.vertex_count;
#else
    /* Raw28 lacks original fog authority. Do not guess or repeat resolution. */
    return 0;
 
#endif
   }
   if(!reserve(c,j,(VkDeviceSize)p->vertices*p->draw.vertex_stride,16,&p->vertex)||
    (p->indices&&!reserve(c,j,(VkDeviceSize)p->indices*2,2,&p->index))||!reserve(c,j,m->ps_bytes,c->alignment,&p->ps)||!reserve(c,j,192*16,c->alignment,&p->vs)||!reserve(c,j,48,c->alignment,&p->frame))return 0;
 
  }else if(s->kind==VSJ_CLEAR){
   VsrClearNative a;
 size_t n;
 const void*data=chunk(s,VSJ_CHUNK_NATIVE_ARGS,&n);
 if(!data||n<sizeof a)return 0;
 memcpy(&a,data,sizeof a);
 
   if(a.version!=VSR_VERSION||(a.flags&~7u)||!a.flags||n!=sizeof a+(size_t)a.count*16||!view_ok(&a.view)||(!a.count&&s->writer_count!=1))return 0;
 
   /* Clears serialize only affected attachments, unlike draws. */
   if((a.flags&1)&&!a.view.depth_only&&p->color==NONE)return 0;
 if((a.flags&6)&&p->depth==NONE)return 0;
   if((a.flags&4)&&!(j->resource[p->depth].source.aspects&VK_IMAGE_ASPECT_STENCIL_BIT))return 0;
 
   for(unsigned i=0;i<a.count;i++){int32_t r[4];
 memcpy(r,(const unsigned char*)data+sizeof a+i*16,16);
 if(r[2]<r[0]||r[3]<r[1])return 0;
 }
   for(unsigned i=0;i<s->use_count;i++){Resource*r=&j->resource[p->use[i]];
 if(!r->fresh||r->source.width!=a.view.physical[0]||r->source.height!=a.view.physical[1]||r->source.layers!=1)return 0;
 }
  }else if(s->kind==VSJ_UPLOAD){
   VsrUploadNative a;
 if(!exact_chunk(s,VSJ_CHUNK_NATIVE_ARGS,&a,sizeof a)||a.version!=VSR_VERSION||s->use_count!=1||!a.width||!a.height)return 0;
 
   Resource*r=&j->resource[p->use[0]];
 if(a.level>=32)return 0;
 uint32_t w=r->source.width>>a.level,h=r->source.height>>a.level;
 size_t bytes;
 const void*data=chunk(s,VSJ_CHUNK_CPU_UPLOAD,&bytes);
 
   if(a.level>=r->source.mips||a.level>=32||a.face>=r->source.layers||a.cube!=(r->source.layers==6)||a.width!=(w?w:1)||a.height!=(h?h:1)||!data||bytes!=(uint64_t)a.width*a.height*4||a.bytes!=bytes||r->source.format!=VK_FORMAT_R8G8B8A8_UNORM||!r->fresh||!reserve(c,j,bytes,4,&p->upload))return 0;
 
  }else if(s->kind==VSJ_VIEW||s->kind==VSJ_TARGET_SELECT){
   VsrStateNative a;
 if(s->use_count||!exact_chunk(s,VSJ_CHUNK_NATIVE_ARGS,&a,sizeof a)||a.version!=VSR_VERSION||!view_ok(&a.view)||a.opcode<VSR_STATE_TARGET||a.opcode>VSR_STATE_POINT||a.scalar_bytes>16||a.resource_count>6)return 0;
   static const uint32_t sizes[7]={0,0,16,12,16,8,8};
   if(s->kind==VSJ_TARGET_SELECT){if(a.opcode!=VSR_STATE_TARGET||a.result!=1||a.scalar_bytes||!a.resource_count)return 0;}
   else if(a.opcode==VSR_STATE_TARGET||a.resource_count||a.scalar_bytes!=sizes[a.opcode])return 0;
 
  }else if(s->kind==VSJ_FINAL_TARGET){
   struct{uint32_t version,reserved;
 uint64_t identity;
 }a;
 if(k+1!=j->commands||s->use_count||!exact_chunk(s,VSJ_CHUNK_NATIVE_ARGS,&a,sizeof a)||a.version!=1||a.reserved)return 0;
 
   for(unsigned i=0;i<j->resources;i++)if(j->resource[i].initial.resource.identity==a.identity&&j->resource[i].fresh&&j->resource[i].source.aspects==VK_IMAGE_ASPECT_COLOR_BIT)j->final=i;
 
  }else return 0;
 
 }
 return j->final!=NONE&&admitted(c)&&vsj_validate(f);
 
}
static int memory_type(VreContext*c,uint32_t mask,VkMemoryPropertyFlags flags,uint32_t*out){for(unsigned i=0;i<c->memory.memoryTypeCount;i++)if((mask&(1u<<i))&&(c->memory.memoryTypes[i].propertyFlags&flags)==flags){*out=i;
 return 1;
 }return 0;
 }
static int allocate(VreContext*c,Job*j,const VkMemoryRequirements*r,VkMemoryPropertyFlags flags,VkDeviceMemory*out){
 uint32_t type;
 if(!r->size||r->size>c->config.gpu_budget||j->gpu_bytes>c->config.gpu_budget-r->size||!((!flags&&memory_type(c,r->memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type))||memory_type(c,r->memoryTypeBits,flags,&type)))return 0;
 
 VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r->size,.memoryTypeIndex=type};
 
 ACQUIRE(VkDeviceMemory,*out,vkAllocateMemory(c->config.device,&a,NULL,&acquired));
 j->gpu_bytes+=r->size;
 return 1;
 
}
static void destroy_job(VreContext*c,Job*j){
 if(!j)return;
 VkDevice d=c->config.device;
 
 if(j->pool)vkDestroyCommandPool(d,j->pool,NULL);
 
 if(j->descriptors)vkDestroyDescriptorPool(d,j->descriptors,NULL);
 
 for(unsigned i=0;i<j->commands;i++){Command*p=&j->command[i];
 if(p->framebuffer)vkDestroyFramebuffer(d,p->framebuffer,NULL);
 if(p->pass)vkDestroyRenderPass(d,p->pass,NULL);
 }
 for(unsigned i=0;i<j->resources;i++){Resource*r=&j->resource[i];
 if(r->sampled)vkDestroyImageView(d,r->sampled,NULL);
 if(r->attachment)vkDestroyImageView(d,r->attachment,NULL);
 if(r->fresh&&r->image)vkDestroyImage(d,r->image,NULL);
 if(r->memory)vkFreeMemory(d,r->memory,NULL);
 }
 if(j->mapped)vkUnmapMemory(d,j->memory);
 if(j->buffer)vkDestroyBuffer(d,j->buffer,NULL);
 if(j->memory)vkFreeMemory(d,j->memory,NULL);
 if(j->fence)vkDestroyFence(d,j->fence,NULL);
 free(j);
 
}
static int image_view(VreContext*c,Resource*r,int attachment){
 VkImageViewCreateInfo a={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=r->image,.viewType=attachment?VK_IMAGE_VIEW_TYPE_2D:(r->source.layers==6?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D),.format=r->source.format,
 .subresourceRange={attachment?r->source.aspects:(r->source.aspects&VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,attachment?1:r->source.mips,0,attachment?1:r->source.layers}};
 
 ACQUIRE(VkImageView,*(attachment?&r->attachment:&r->sampled),vkCreateImageView(c->config.device,&a,NULL,&acquired));return 1;
 
}
static void transition(Job*j,Resource*r,VkImageLayout layout){
 /* A repeated attachment layout still needs a memory dependency between passes. */
 
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=r->layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.oldLayout=r->layout,.newLayout=layout,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=r->image,.subresourceRange={r->source.aspects,0,r->source.mips,0,r->source.layers}};
 
 vkCmdPipelineBarrier(j->cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
 r->layout=layout;
 
}
static int resources(VreContext*c,Job*j){
 VkDevice d=c->config.device;
 
 VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=c->config.family};
 ACQUIRE(VkCommandPool,j->pool,vkCreateCommandPool(d,&pool,NULL,&acquired));
 
 VkCommandBufferAllocateInfo cmd={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=j->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
 ACQUIRE(VkCommandBuffer,j->cmd,vkAllocateCommandBuffers(d,&cmd,&acquired));
 
 VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 ACQUIRE(VkFence,j->fence,vkCreateFence(d,&fence,NULL,&acquired));
 
 VkDeviceSize packed=j->packed?j->packed:16;
 VkBufferCreateInfo b={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=packed,.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 
 ACQUIRE(VkBuffer,j->buffer,vkCreateBuffer(d,&b,NULL,&acquired));
 VkMemoryRequirements m;
 vkGetBufferMemoryRequirements(d,j->buffer,&m);
 if(m.size>c->config.cpu_budget-sizeof(Job)-sizeof(VreContext))return 0;
 
 if(!allocate(c,j,&m,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&j->memory)||vkBindBufferMemory(d,j->buffer,j->memory,0)!=VK_SUCCESS)return 0;
 void*mapped=NULL;if(vkMapMemory(d,j->memory,0,packed,0,&mapped)!=VK_SUCCESS)return 0;j->mapped=mapped;
 
 memset(j->mapped,0,(size_t)packed);
 
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,3*j->commands},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4*j->commands},{VK_DESCRIPTOR_TYPE_SAMPLER,4*j->commands}};
 
 VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=j->commands,.poolSizeCount=3,.pPoolSizes=sizes};
 ACQUIRE(VkDescriptorPool,j->descriptors,vkCreateDescriptorPool(d,&dp,NULL,&acquired));
 
 for(unsigned i=0;i<j->resources;i++){
  Resource*r=&j->resource[i];
 r->image=r->source.image;
 r->layout=r->source.layout;
 
  if(r->fresh){VkImageCreateInfo a={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=r->source.flags,.imageType=VK_IMAGE_TYPE_2D,.format=r->source.format,.extent={r->source.width,r->source.height,1},.mipLevels=r->source.mips,.arrayLayers=r->source.layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|(r->source.aspects==VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 r->image=VK_NULL_HANDLE;
 
   ACQUIRE(VkImage,r->image,vkCreateImage(d,&a,NULL,&acquired));
 vkGetImageMemoryRequirements(d,r->image,&m);
 if(!allocate(c,j,&m,0,&r->memory)||vkBindImageMemory(d,r->image,r->memory,0)!=VK_SUCCESS)return 0;
 r->layout=VK_IMAGE_LAYOUT_UNDEFINED;
 
   if(!image_view(c,r,1))return 0;
 
  }
  if(!image_view(c,r,0))return 0;
 
 }
 return 1;
 
}
static void copy_initial(Job*j,Resource*r){
 Resource source=*r;
 source.image=r->source.image;
 source.layout=r->source.layout;
 
 transition(j,&source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 transition(j,r,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 
 for(unsigned mip=0;mip<r->source.mips;mip++)for(unsigned bit=0;bit<3;bit++)if(r->source.aspects&(1u<<bit)){
  uint32_t w=r->source.width>>mip,h=r->source.height>>mip;
 
  VkImageCopy region={.srcSubresource={1u<<bit,mip,0,r->source.layers},.dstSubresource={1u<<bit,mip,0,r->source.layers},.extent={w?w:1,h?h:1,1}};
 
  vkCmdCopyImage(j->cmd,source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,r->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 
 }
 transition(j,&source,r->source.layout);
 
}
static int pass(VreContext*c,Job*j,Command*p,const D3D8SceneViewState*v){
 VkAttachmentDescription desc[2]={{0}};
 VkImageView views[2];
 VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
 unsigned n=0;
 
 uint32_t refs[2]={p->color,p->depth};
 for(unsigned i=0;i<2;i++)if(refs[i]!=NONE){Resource*r=&j->resource[refs[i]];
 VkImageLayout layout=i?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 
  transition(j,r,layout);
 desc[n]=(VkAttachmentDescription){.format=r->source.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=layout,.finalLayout=layout};
 views[n]=r->attachment;
 if(i)depth.attachment=n;
 else color.attachment=n;
 n++;
 
 }
 VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=p->color==NONE?0:1,.pColorAttachments=p->color==NONE?NULL:&color,.pDepthStencilAttachment=p->depth==NONE?NULL:&depth};
 
 VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=n,.pAttachments=desc,.subpassCount=1,.pSubpasses=&sub};
 ACQUIRE(VkRenderPass,p->pass,vkCreateRenderPass(c->config.device,&rp,NULL,&acquired));
 
 VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=p->pass,.attachmentCount=n,.pAttachments=views,.width=v->physical[0],.height=v->physical[1],.layers=1};
 ACQUIRE(VkFramebuffer,p->framebuffer,vkCreateFramebuffer(c->config.device,&fb,NULL,&acquired));
 
 VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=p->pass,.framebuffer=p->framebuffer,.renderArea={{0,0},{v->physical[0],v->physical[1]}}};
 vkCmdBeginRenderPass(j->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
 return 1;
 
}
static int draw(VreContext*c,Job*j,Command*p){
 const VsjCommand*s=p->command;
 D3D8SceneGpuDraw*m=&p->material;
 size_t n;
 const unsigned char*geometry=chunk(s,VSJ_CHUNK_GEOMETRY,&n);
 
 if(p->points){const unsigned char*idx=chunk(s,VSJ_CHUNK_INDICES,&n);
 for(unsigned i=0;i<p->vertices;i++){uint16_t at;
 memcpy(&at,idx+i*2,2);
 memcpy(j->mapped+p->vertex+(size_t)i*p->draw.vertex_stride,geometry+(size_t)(at<p->draw.vertex_count?at:0)*p->draw.vertex_stride,p->draw.vertex_stride);
 }}
 else memcpy(j->mapped+p->vertex,geometry,(size_t)p->vertices*p->draw.vertex_stride);
 
 if(p->indices){const void*indices=chunk(s,VSJ_CHUNK_INDICES,&n);
 memcpy(j->mapped+p->index,indices,p->indices*2);
 }
 memcpy(j->mapped+p->ps,m->ps_constants,m->ps_bytes);
 memcpy(j->mapped+p->frame,m->frame_constants,48);
 
 if(s->kind==VSJ_PROGRAM_DRAW){const void*constants=chunk(s,VSJ_CHUNK_CONSTANTS,&n);
 memcpy(j->mapped+p->vs,constants,192*16);
 }
 VkDescriptorSetLayout dsl=(VkDescriptorSetLayout)(uintptr_t)m->descriptor_layout;
 VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=j->descriptors,.descriptorSetCount=1,.pSetLayouts=&dsl};
 
 ACQUIRE(VkDescriptorSet,p->descriptors,vkAllocateDescriptorSets(c->config.device,&ai,&acquired));
 
 VkDescriptorBufferInfo bi[3]={{j->buffer,0,m->ps_bytes},{j->buffer,0,192*16},{j->buffer,0,48}};
 VkDescriptorImageInfo ii[8]={{0}};
 VkWriteDescriptorSet w[11]={{0}};
 
 for(unsigned i=0;i<3;i++)w[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=p->descriptors,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,.pBufferInfo=&bi[i]};
 
 for(unsigned i=0;i<4;i++){Resource*r=&j->resource[p->texture[i]];
 if(r->fresh)transition(j,r,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 
  ii[i].imageView=r->sampled;
 ii[i].imageLayout=r->layout;
 ii[4+i].sampler=(VkSampler)(uintptr_t)m->samplers[i];
 
  w[3+i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=p->descriptors,.dstBinding=8+i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.pImageInfo=&ii[i]};
 
  w[7+i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=p->descriptors,.dstBinding=16+i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER,.pImageInfo=&ii[4+i]};
 
 }
 vkUpdateDescriptorSets(c->config.device,11,w,0,NULL);
 
 if(!pass(c,j,p,&p->draw.view))return 0;
 
 vkCmdBindPipeline(j->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipeline)(uintptr_t)m->pipeline);
 
 uint32_t dynamic[3]={(uint32_t)p->ps,(uint32_t)p->vs,(uint32_t)p->frame};
 vkCmdBindDescriptorSets(j->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipelineLayout)(uintptr_t)m->pipeline_layout,0,1,&p->descriptors,3,dynamic);
 
 VkViewport viewport;
 memcpy(&viewport,p->draw.view.viewport,sizeof viewport);
 vkCmdSetViewport(j->cmd,0,1,&viewport);
 
 VkRect2D scissor={{p->draw.view.scissor_xy[0],p->draw.view.scissor_xy[1]},{p->draw.view.scissor_wh[0],p->draw.view.scissor_wh[1]}};
 vkCmdSetScissor(j->cmd,0,1,&scissor);
 
 vkCmdSetStencilReference(j->cmd,VK_STENCIL_FACE_FRONT_AND_BACK,m->stencil_reference);
 vkCmdSetStencilCompareMask(j->cmd,VK_STENCIL_FACE_FRONT_AND_BACK,m->stencil_compare_mask);
 vkCmdSetStencilWriteMask(j->cmd,VK_STENCIL_FACE_FRONT_AND_BACK,m->stencil_write_mask);
 
 vkCmdSetDepthBias(j->cmd,m->depth_bias[0],m->depth_bias[1],m->depth_bias[2]);
 vkCmdSetBlendConstants(j->cmd,m->blend);
 
 vkCmdBindVertexBuffers(j->cmd,0,1,&j->buffer,&p->vertex);
 
 if(p->points)vkCmdDraw(j->cmd,4,p->vertices,0,0);
 
 else if(p->indices){vkCmdBindIndexBuffer(j->cmd,j->buffer,p->index,VK_INDEX_TYPE_UINT16);
 vkCmdDrawIndexed(j->cmd,p->indices,1,0,0,0);
 }
 else vkCmdDraw(j->cmd,p->vertices,1,0,0);
 
 vkCmdEndRenderPass(j->cmd);
 return 1;
 
}
static void clear_rect(Job*j,const VsrClearNative*a,const VkClearAttachment*attachments,unsigned n,const int32_t*input){
 uint32_t width=a->view.physical[0],height=a->view.physical[1];
 VkClearRect rect={.rect={{0,0},{width,height}},.baseArrayLayer=0,.layerCount=1};
 
 if(input){double sx=a->target_active?(double)width/a->view.logical[0]:a->view.canvas_scale[0],sy=a->target_active?(double)height/a->view.logical[1]:a->view.canvas_scale[1];
 double pos[4]={input[0]*sx,input[1]*sy,input[2]*sx,input[3]*sy};
 
  for(unsigned i=0;i<4;i++){double max=(i&1)?height:width;
 if(pos[i]<0)pos[i]=0;
 if(pos[i]>max)pos[i]=max;
 }
  uint32_t l=(uint32_t)(pos[0]+0.5),t=(uint32_t)(pos[1]+0.5),r=(uint32_t)(pos[2]+0.5),b=(uint32_t)(pos[3]+0.5);
 if(l>=r||t>=b)return;
 rect.rect=(VkRect2D){{(int32_t)l,(int32_t)t},{r-l,b-t}};
 
 }
 vkCmdClearAttachments(j->cmd,n,attachments,1,&rect);
 
}
static int record(VreContext*c,Job*j){
 VkCommandBufferBeginInfo b={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
 if(vkBeginCommandBuffer(j->cmd,&b)!=VK_SUCCESS)return 0;
 
 for(unsigned i=0;i<j->resources;i++)if(j->resource[i].fresh)copy_initial(j,&j->resource[i]);
 
 for(unsigned k=0;k<j->commands;k++){
  Command*p=&j->command[k];
 const VsjCommand*s=p->command;
 
  if(s->kind==VSJ_PROGRAM_DRAW||s->kind==VSJ_UP_DRAW){if(!draw(c,j,p))return 0;
 }
  else if(s->kind==VSJ_CLEAR){VsrClearNative a;
 size_t n;
 const unsigned char*bytes=chunk(s,VSJ_CHUNK_NATIVE_ARGS,&n);
 memcpy(&a,bytes,sizeof a);
 if(!pass(c,j,p,&a.view))return 0;
 
   VkClearAttachment att[2]={{0}};
 unsigned count=0;
 if((a.flags&1)&&p->color!=NONE){att[count].aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
 float*color=att[count].clearValue.color.float32;
 color[0]=((a.color>>16)&255)/255.f;
 color[1]=((a.color>>8)&255)/255.f;
 color[2]=(a.color&255)/255.f;
 color[3]=((a.color>>24)&255)/255.f;
 count++;
 }
   if((a.flags&6)&&p->depth!=NONE){att[count].aspectMask=((a.flags&2)?VK_IMAGE_ASPECT_DEPTH_BIT:0)|((a.flags&4)?VK_IMAGE_ASPECT_STENCIL_BIT:0);
 att[count].clearValue.depthStencil=(VkClearDepthStencilValue){a.depth,a.stencil&255u};
 count++;
 }
   if(!a.count)clear_rect(j,&a,att,count,NULL);
 else for(unsigned i=0;i<a.count;i++){int32_t rect[4];
 memcpy(rect,bytes+sizeof a+i*16,16);
 clear_rect(j,&a,att,count,rect);
 }vkCmdEndRenderPass(j->cmd);
 
  }else if(s->kind==VSJ_UPLOAD){VsrUploadNative a;
 exact_chunk(s,VSJ_CHUNK_NATIVE_ARGS,&a,sizeof a);
 size_t n;
 const void*bytes=chunk(s,VSJ_CHUNK_CPU_UPLOAD,&n);
 memcpy(j->mapped+p->upload,bytes,n);
 Resource*r=&j->resource[p->use[0]];
 transition(j,r,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 
   VkBufferImageCopy region={.bufferOffset=p->upload,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,a.level,a.face,1},.imageExtent={a.width,a.height,1}};
 vkCmdCopyBufferToImage(j->cmd,j->buffer,r->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 
  }
 }
 transition(j,&j->resource[j->final],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 
 return vkEndCommandBuffer(j->cmd)==VK_SUCCESS;
 
}
VreContext*vre_create(const VreConfig*config){
 if(!config||!config->physical||!config->device||!config->queue||!config->device_epoch||!config->owner||!config->admitted||!config->describe||!config->material_valid||config->cpu_budget<sizeof(Job)+sizeof(VreContext)||config->cpu_budget>VRE_MAX_BYTES||!config->gpu_budget||config->gpu_budget>VRE_MAX_BYTES||!config->owner(config->user)||!config->admitted(config->user,config->device_epoch))return NULL;
 
 VreContext*c=calloc(1,sizeof*c);
 if(!c)return NULL;
 c->config=*config;
 VkPhysicalDeviceProperties props;
 vkGetPhysicalDeviceProperties(config->physical,&props);
 vkGetPhysicalDeviceMemoryProperties(config->physical,&c->memory);
 c->alignment=props.limits.minUniformBufferOffsetAlignment;
 if(!c->alignment)c->alignment=1;
 
 if(!c->memory.memoryTypeCount||c->memory.memoryTypeCount>32||c->alignment>VRE_MAX_BYTES){free(c);
 return NULL;
 }return c;
 
}
int vre_submit(VreContext*c,const VsjFrame*f,VreOutput*out){
 if(!enter(c))return VRE_OWNER;
 if(c->job)return leave(c,VRE_BUSY);
 if(!f||!out||!admitted(c)||c->nonce==UINT64_MAX)return leave(c,VRE_INVALID);
 
 Job*j=calloc(1,sizeof*j);
 if(!j)return leave(c,VRE_BUDGET);
 if(!preflight(c,j,f)){destroy_job(c,j);
 return leave(c,VRE_INVALID);
 }
 if(!resources(c,j)||!record(c,j)){destroy_job(c,j);
 return leave(c,VRE_GPU);
 }
 if(!admitted(c)||!vsj_validate(f)){destroy_job(c,j);
 return leave(c,VRE_INVALID);
 }for(unsigned i=0;i<j->commands;i++)if(j->command[i].command->kind==VSJ_PROGRAM_DRAW||j->command[i].command->kind==VSJ_UP_DRAW)if(!c->config.material_valid(c->config.user,&j->command[i].material)){destroy_job(c,j);
 return leave(c,VRE_INVALID);
 }
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&j->cmd};
 VkResult result=vkQueueSubmit(c->config.queue,1,&submit,j->fence);
 
 if(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST){destroy_job(c,j);
 return leave(c,VRE_GPU);
 }
 j->submitted=1;
 j->lost=result==VK_ERROR_DEVICE_LOST;
 j->ticket=++c->nonce;
 c->job=j;
 Resource*r=&j->resource[j->final];
 
 *out=(VreOutput){j->ticket,r->image,r->sampled,r->layout,r->source.format,r->source.width,r->source.height,j->fence};
 return leave(c,j->lost?VRE_QUARANTINED:VRE_OK);
 
}
int vre_poll(VreContext*c,uint64_t ticket){
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;
 if(!j||!ticket||ticket!=j->ticket)return leave(c,VRE_INVALID);
 if(j->lost)return leave(c,VRE_LOST);
 if(j->complete)return leave(c,VRE_OK);
 
 VkResult status=vkGetFenceStatus(c->config.device,j->fence);
 if(status==VK_SUCCESS){j->complete=1;
 return leave(c,VRE_OK);
 }if(status==VK_NOT_READY)return leave(c,VRE_WAIT);
 j->lost=1;
 return leave(c,VRE_LOST);
 
}
int vre_finish(VreContext*c,uint64_t ticket){
 int status=vre_poll(c,ticket);
 if(status!=VRE_OK)return status;
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;
 if(!j||j->ticket!=ticket||!j->complete)return leave(c,VRE_INVALID);
 for(unsigned i=0;i<8;i++)if(j->consumers[i])return leave(c,VRE_BUSY);
 c->job=NULL;
 destroy_job(c,j);
 return leave(c,VRE_OK);
 
}
int vre_destroy(VreContext**ptr){if(!ptr||!*ptr)return VRE_INVALID;
 VreContext*c=*ptr;
 if(!enter(c))return VRE_OWNER;
 if(c->job)return leave(c,VRE_BUSY);
 free(c);
 *ptr=NULL;
 return VRE_OK;
 }

int vre_output(VreContext*c,uint64_t ticket,VreOutput*out){
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;
 if(!out||!j||!ticket||j->ticket!=ticket||j->lost||!j->submitted||!admitted(c)||!vsj_validate(j->frame))return leave(c,VRE_INVALID);
 Resource*r=&j->resource[j->final];*out=(VreOutput){j->ticket,r->image,r->sampled,r->layout,r->source.format,r->source.width,r->source.height,j->fence};return leave(c,VRE_OK);
}
int vre_consumer_acquire(VreContext*c,uint64_t ticket,uint64_t*out){
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;
 if(!out||!j||!ticket||j->ticket!=ticket||j->lost||!j->complete||!admitted(c)||!vsj_validate(j->frame)||c->consumer_nonce==UINT64_MAX)return leave(c,VRE_INVALID);
 for(unsigned i=0;i<8;i++)if(!j->consumers[i]){*out=j->consumers[i]=++c->consumer_nonce;return leave(c,VRE_OK);}
 return leave(c,VRE_BUSY);
}
int vre_consumer_release(VreContext*c,uint64_t ticket,uint64_t token){
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;if(!j||!ticket||j->ticket!=ticket||!token)return leave(c,VRE_INVALID);
 for(unsigned i=0;i<8;i++)if(j->consumers[i]==token){j->consumers[i]=0;return leave(c,VRE_OK);}
 return leave(c,VRE_INVALID);
}

int vre_source_stamp(VreContext*c,uint64_t ticket,VsjStamp*out){
 if(!enter(c))return VRE_OWNER;
 Job*j=c->job;
 if(!out||!j||!ticket||j->ticket!=ticket||j->lost||!admitted(c)||!vsj_validate(j->frame))return leave(c,VRE_INVALID);
 const VsjStamp*s=vsj_stamp(j->frame);if(!s)return leave(c,VRE_INVALID);*out=*s;return leave(c,VRE_OK);
}
