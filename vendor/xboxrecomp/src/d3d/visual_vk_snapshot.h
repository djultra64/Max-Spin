/* Proposed real Vulkan content snapshots. Own work djultra64. */
#ifndef TOPSPIN_VISUAL_VK_SNAPSHOT_H
#define TOPSPIN_VISUAL_VK_SNAPSHOT_H
#include <vulkan/vulkan.h>
#include "visual_frame_packet.h"
#define VKS_MAX_OBJECTS 64u
#define VKS_MAX_FENCES 8u
#define VKS_MAX_BYTES (256ull*1024*1024)
typedef struct VksContext VksContext;
typedef struct VksLease VksLease;
typedef struct {
 VkImage image; VkImageLayout layout;VkFormat format;VkImageAspectFlags aspects;
 VkImageUsageFlags usage;VkImageCreateFlags flags;VkSampleCountFlagBits samples;
 uint32_t width,height,mips,layers;
 uint64_t storage_identity,content_version,device_generation;
} VksSource;
typedef struct {
 void *user;
 /* Must execute on the actual GPU executor thread (future93 adapter), OR
  * hold an actual backend lock also used by its independent ack_thread.
  * The guest CPU token does NOT serialize that thread; a no-op lock in the
  * real backend is invalid. Serialize resolver/layout/queue with ALL
  * original uploads/clears/draws. Same queue, no pending unsubmitted writes.
  * resolve returns the CURRENT exact immutable-until-unlock content version.
  * storage_identity canonicalizes aliases to the same backing allocation. */
 int (*lock)(void*);void(*unlock)(void*);
 int (*resolve)(void*,const VfpResource*,VksSource*);
 int (*generation)(void*,VfpGeneration*,uint64_t *device_generation);
} VksAccess;
/* Single-owner/non-reentrant context. Access callbacks and Vulkan device
 * outlive context; no implicit global/device/queue discovery/fallback. */
VksContext*vks_create(VkPhysicalDevice,VkDevice,VkQueue,uint32_t queue_family,const VksAccess*,uint64_t max_bytes);
VfpProvider vks_provider(VksContext*);
/* Borrow exact retained version while the owning VfpPacket remains alive.
 * Does not consult mutable current source or add a reference. */
int vks_resource(VksContext*,const VfpResource*,void **borrowed_lease);
/* Immutable borrowed handles; content cannot be rebound/mutated by caller.
 * Own copy fence completes before retain succeeds. Not submission authority. */
int vks_image(VksContext*,void *lease,VkImage*,VkFormat*,uint32_t *width,uint32_t *height);
/* Transfer OWNERSHIP of an already submitted consumer fence from SAME device
 * and ordered queue. First success means provider eventually destroys it. The same already-owned
 * fence may be attached to other leases (shared ownership inside provider).
 * Duplicate attachment to one lease rejects. On failure a NEW fence stays
 * caller-owned; an already-registered fence remains provider-owned. Caller
 * must finish an unregistered consumer before releasing its lease. */
int vks_add_consumer_fence(VksContext*,void *lease,VkFence);
/* Host readback of one mip/layer: color-native bytes or depth-aspect bytes.
 * D24S8/D32S8 return4B depth-plane texels, NOT packed stencil contents. The
 * snapshot image itself keeps both aspects. Unsupported formats reject; caller
 * supplies exact size. No guest or GPU color rewrite. */
int vks_readback(VksContext*,void *lease,uint32_t mip,uint32_t layer,void *out,size_t bytes);
/* Release queues retirement; only collect after ALL real fences complete.
 * destroy returns0 with context untouched if a live/pending lease remains. */
unsigned vks_collect(VksContext*);
int vks_destroy(VksContext**);
uint64_t vks_charged_bytes(const VksContext*);
#endif
