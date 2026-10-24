/* Isolated sprite-only compute Super-xBR. Own adapter: djultra64. */
#ifndef TOPSPIN_SPRITE_XBR_VK_H
#define TOPSPIN_SPRITE_XBR_VK_H
#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>
#define SXVK_BUDGET (64ull*1024*1024)
#define SXVK_CACHE 32u
#define SXVK_ALGORITHM 1u
/* Admission is a canonical whole independent UI resource, never an atlas/RT.
 * identity/version identify the original allocation's CONTENT (exporter94),
 * NOT VkImage handle, guest VA, or pointer identity. */
typedef struct {
 uint64_t allocation,version,device_generation;
 uint32_t profile,width,height,guest_format,factor;
} SxVkKey;
typedef struct {
 VkImage image;VkFormat format;VkImageLayout layout;
 uint32_t width,height;uint64_t charged_bytes;
 void *lease;
} SxVkSnapshot;
typedef struct {
 void *user;
 /* Only real executor93; flush ALL earlier original writes before acquisition.
  * acquire MUST freeze exact key content in a sampled immutable image, copied
  * via provider94. On success one lease, actual VkMemoryRequirements charge
  * <=allowance. On failure no lease. All callbacks execute on same owner.
  * The original allocation can later be changed/retired; snapshot cannot. */
 int(*owner)(void*);
 int(*acquire)(void*,const SxVkKey*,uint64_t allowance,SxVkSnapshot*);
 void(*release)(void*,SxVkSnapshot*);
 /* Actual charges quarantined by failed provider acquisitions (not returned
  * in snapshots). Included in the SAME64MiB budget. NULL means none. */
 uint64_t(*external_bytes)(void*);
} SxVkAccess;
typedef struct {uint64_t allocation,version;uint32_t profile;} SxVkAuth;
/* Authentication follows actual uploaded content. A checked CPU pointer alone
 * cannot authorize an8ms cache hit whose GPU bytes might be older. */
static inline int sxvk_auth(SxVkAuth*a,uint32_t profile,int uploaded,uint64_t allocation,uint64_t version){
 if(!a)return 0;
 if(!profile||profile>3||!allocation||!version||(uploaded!=0&&uploaded!=1)){*a=(SxVkAuth){0};return 0;}
 if(uploaded)*a=(SxVkAuth){allocation,version,profile};
 return a->profile==profile&&a->allocation==allocation&&a->version==version;
}
/* Initial proven color-sprite route only: stage0 simple2D, no dependent or
 * multitexture interpretation, no color-key/alpha-test boundary rewrite. */
static inline int sxvk_draw_policy(unsigned stage,uint32_t texture_program,unsigned explicit_nv2a,uint32_t texture_control,unsigned alpha_test){return stage==0&&texture_program==1&&explicit_nv2a&&!(texture_control&3u)&&!alpha_test;}
typedef struct SxVkContext SxVkContext;
/* Both modules are compiled from shaders/sprite_xbr.comp, FINAL_RGBA8=0/1.
 * Checks sampled/storage format support and graphics+compute queue; no fake capability. */
int sxvk_supported(VkPhysicalDevice,uint32_t family);
SxVkContext *sxvk_create(VkPhysicalDevice,VkDevice,VkQueue,uint32_t family,uint64_t generation,const SxVkAccess*,const uint32_t *float_spv,size_t float_bytes,const uint32_t *rgba_spv,size_t rgba_bytes);
/* Returns completed owned view or NULL => sample Original. Original logical
 * dimensions/sampler/UV/texture metadata MUST remain unchanged. Cache outputs
 * are pinned (no in-flight eviction). No normal3D admission occurs here.
 * Synchronous first miss is bounded by a real1s Vulkan fence wait; timeout/
 * DEVICE_LOST quarantines all resources and returns Original, never frees
 * pending work. Cache saturation/budget/allocation failure returns Original. */
VkImageView sxvk_get(SxVkContext*,const SxVkKey*);
/* Collects only completed failed-miss jobs, owner-only. */
void sxvk_collect(SxVkContext*);
uint64_t sxvk_bytes(const SxVkContext*);
/* Caller proves vkDeviceWaitIdle succeeded AFTER all backend draws using
 * returned views. Failed idle MUST NOT call this. Refuses pending own work. */
int sxvk_destroy_idle(SxVkContext**);
#endif
