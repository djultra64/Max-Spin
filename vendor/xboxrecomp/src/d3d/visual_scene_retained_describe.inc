/* Include at bottom of actual visual_vk_snapshot.c, not d3d8_vk.c. */
#include "visual_scene_retained_describe.h"
int vks_describe(VksContext*c,const VfpResource*r,void*lease,VksSource*out){
 if(!c||!r||!lease||!out||c->busy)return 0;
 VksLease*l=c->leases;for(;l&&l!=lease;l=l->next){}
 if(!l||!validate(c,r,l))return 0;
 VksSource s=l->source;s.image=l->image;s.layout=l->layout;
 s.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
 *out=s;return 1;
}
