/* Bounded actual-image identity/version registry. Own work djultra64. */
#ifndef TOPSPIN_VK_RESOURCE_VERSIONS_H
#define TOPSPIN_VK_RESOURCE_VERSIONS_H
#include "visual_vk_snapshot.h"
#include <string.h>
#define VKR_ENTRIES 8192u
typedef struct {VksSource images[VKR_ENTRIES];uint64_t next_identity;unsigned count,broken;} VkResourceVersions;
static inline int vkr_add(VkResourceVersions*r,const VksSource*source){
 if(!r||!source||!source->image||r->broken)return 0;
 unsigned slot=VKR_ENTRIES;
 for(unsigned i=0;i<VKR_ENTRIES;i++){if(r->images[i].image==source->image){r->broken=1;return 0;}if(!r->images[i].image&&slot==VKR_ENTRIES)slot=i;}
 if(slot==VKR_ENTRIES||r->next_identity==UINT64_MAX){r->broken=1;return 0;}
 r->images[slot]=*source;r->images[slot].storage_identity=++r->next_identity;r->images[slot].content_version=0;r->count++;return 1;
}
static inline void vkr_forget(VkResourceVersions*r,VkImage image){if(!r||!image)return;for(unsigned i=0;i<VKR_ENTRIES;i++)if(r->images[i].image==image){memset(&r->images[i],0,sizeof r->images[i]);r->count--;return;}}
static inline VksSource*vkr_image(VkResourceVersions*r,VkImage image){if(!r||r->broken||!image)return NULL;for(unsigned i=0;i<VKR_ENTRIES;i++)if(r->images[i].image==image)return&r->images[i];return NULL;}
static inline int vkr_touch(VkResourceVersions*r,VkImage image){VksSource*s=vkr_image(r,image);if(!s||s->content_version==UINT64_MAX){if(r)r->broken=1;return 0;}s->content_version++;return 1;}
static inline int vkr_find(VkResourceVersions*r,uint64_t identity,uint64_t version,VksSource*out){if(!r||r->broken||!out||!identity||!version)return 0;for(unsigned i=0;i<VKR_ENTRIES;i++)if(r->images[i].image&&r->images[i].storage_identity==identity&&r->images[i].content_version==version){*out=r->images[i];return 1;}return 0;}
#endif
