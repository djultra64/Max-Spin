/* Bottom actual d3d8_vk.c. Owner-only state after explicit startup. djultra64. */
#include "visual_scene_wsi.h"
/* ACTIVE1: rendering/normal present; RETIRE2: acquired image is drained and
 * awaits original safe-boundary maintenance; RETIRED3: actual epoch changed,
 * consumer still owns its acknowledgement token. No idle callback waits. */
static int scene_wsi_enabled,scene_wsi_broken,scene_wsi_refresh_pending;
static uint64_t scene_wsi_epoch,scene_wsi_nonce,scene_wsi_tokens[8];
static unsigned char scene_wsi_state[8];
static int scene_wsi_owner(void){return vk.ready&&sn_enabled&&sn_owner&&sn_owner(sn_owner_user);}
static unsigned scene_wsi_count(void){unsigned n=0;for(unsigned i=0;i<8;i++)n+=scene_wsi_state[i]==1||scene_wsi_state[i]==2;return n;}
static unsigned scene_wsi_retiring(void){unsigned n=0;for(unsigned i=0;i<8;i++)n+=scene_wsi_state[i]==2;return n;}
/* BEFORE original swapchain_make: never retire while an active consumer may
 * still reference the images. A drained acquired image can retire only at the
 * original safe boundary, with its token retained for exact acknowledgement. */
static int scene_wsi_defer(void){
 if(!scene_wsi_enabled)return 0;
 if(scene_wsi_epoch==UINT64_MAX)scene_wsi_broken=1;
 if(scene_wsi_broken)return 1;
 unsigned n=scene_wsi_count();return n&&!(n==1&&scene_wsi_retiring()==1&&!vk.frame_open);
}
/* Original code only logs wait failures. Under explicit WSI ownership the
 * failure must revoke retirement before destroying a possibly live swap. */
static int scene_wsi_wait_ok(VkResult result){
 if(!scene_wsi_enabled)return 1;
 if(result!=VK_SUCCESS)scene_wsi_broken=1;
 return !scene_wsi_broken;
}
/* Only AFTER actual new creation/failure and old swap destruction. */
static void scene_wsi_changed(void){
 if(!scene_wsi_enabled)return;
 if(scene_wsi_epoch==UINT64_MAX){scene_wsi_broken=1;return;}
 scene_wsi_epoch++;scene_wsi_refresh_pending=0;
 for(unsigned i=0;i<8;i++)if(scene_wsi_state[i]==2)scene_wsi_state[i]=3;
}
int d3d8_vk_scene_wsi_configure(void){if(vk.ready||scene_wsi_enabled)return 0;scene_wsi_enabled=1;return 1;}
int d3d8_vk_scene_wsi_borrow(VspWsiLease*out){
 if(!out||!scene_wsi_enabled||scene_wsi_broken||scene_wsi_retiring()||!scene_ready()||!scene_wsi_epoch||scene_wsi_nonce==UINT64_MAX||!vk.surface||!vk.swap||!vk.swap_n||vk.swap_n>VSP_WSI_IMAGES||!vk.swap_ext.width||!vk.swap_ext.height||vk.swap_ext.width>INT32_MAX||vk.swap_ext.height>INT32_MAX)return 0;
 unsigned slot=0;for(;slot<8&&scene_wsi_tokens[slot];slot++){}if(slot==8)return 0;
 VspWsiLease s={.version=1,.bytes=sizeof s,.width=vk.swap_ext.width,.height=vk.swap_ext.height,.format=vk.swap_fmt,.count=vk.swap_n,.device_epoch=sn_device_generation,.swap_epoch=scene_wsi_epoch,.token=++scene_wsi_nonce,.surface=vk.surface,.swap=vk.swap};
 for(unsigned i=0;i<s.count;i++){if(!vk.swap_img[i])return 0;s.images[i]=vk.swap_img[i];}scene_wsi_tokens[slot]=s.token;scene_wsi_state[slot]=1;*out=s;return 1;
}
int d3d8_vk_scene_wsi_validate(const VspWsiLease*s){
 if(!s||!scene_wsi_enabled||scene_wsi_broken||!scene_ready()||s->version!=1||s->bytes!=sizeof*s||!s->token||s->device_epoch!=sn_device_generation||s->swap_epoch!=scene_wsi_epoch||s->surface!=vk.surface||s->swap!=vk.swap||s->width!=vk.swap_ext.width||s->height!=vk.swap_ext.height||s->format!=(uint32_t)vk.swap_fmt||s->count!=vk.swap_n||!s->count||s->count>8)return 0;
 int found=0;for(unsigned i=0;i<8;i++)found|=scene_wsi_tokens[i]==s->token&&scene_wsi_state[i]==1;if(!found)return 0;for(unsigned i=0;i<s->count;i++)if(s->images[i]!=vk.swap_img[i])return 0;return 1;
}
int d3d8_vk_scene_wsi_release(uint64_t token){
 if(!token||!scene_wsi_owner())return 0;
 for(unsigned i=0;i<8;i++)if(scene_wsi_tokens[i]==token&&scene_wsi_state[i]!=2){scene_wsi_tokens[i]=0;scene_wsi_state[i]=0;return 1;}
 return 0;
}
int d3d8_vk_scene_wsi_refresh(void){if(!scene_wsi_enabled||scene_wsi_broken||!scene_wsi_owner())return 0;scene_wsi_refresh_pending=1;return 1;}
int d3d8_vk_scene_wsi_retire(uint64_t token){
 if(!token||!scene_wsi_enabled||scene_wsi_broken||!scene_wsi_owner())return 0;
 for(unsigned i=0;i<8;i++)if(scene_wsi_tokens[i]==token){
  if(scene_wsi_state[i]==3){scene_wsi_tokens[i]=0;scene_wsi_state[i]=0;return 1;}
  if(scene_wsi_state[i]==1)scene_wsi_state[i]=2;
  return 0;
 }return 0;
}
/* Explicit original Present safe boundary ONLY (after original frame wait and,
 * on the normal path, QueuePresent). This may wait through original make;
 * never call it from the nonblocking idle visual service. */
int d3d8_vk_scene_wsi_maintenance(void){
 if(!scene_wsi_enabled||scene_wsi_broken||!scene_wsi_owner()||vk.frame_open||(!scene_wsi_refresh_pending&&!scene_wsi_retiring())||scene_wsi_defer())return 0;
 uint64_t old=scene_wsi_epoch;swapchain_make();return scene_wsi_epoch!=old;
}
