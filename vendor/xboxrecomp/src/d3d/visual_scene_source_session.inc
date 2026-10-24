/* Bottom actual backend; actual owner only, explicit continuous session.
 * No windows, clocks, guest calls, queue submission or fabricated readiness.
 * Own work: djultra64. */
#include "visual_scene_source_session.h"
#include "visual_scene_journal_backend.h"
#include "visual_scene_host_policy.h"
static VssConfig source_config;
static int source_configured,source_busy,source_reentered;
static uint64_t source_session,source_epoch,source_last;
static uint32_t source_failure,source_notified;
static int source_owner(void){
 return vk.ready&&sn_enabled&&sn_owner&&sn_owner(sn_owner_user)&&source_config.owner(source_config.user)==1;
}
static void source_fail(uint32_t reason){if(!source_failure)source_failure=reason;}
static void source_notify(void){
 if(source_failure&&!source_notified){
  source_notified=1;source_config.failure(source_config.user,source_session,source_failure);
 }
}
static int source_enter(void){
 if(source_busy){source_reentered=1;source_fail(VSS_REENTRY);return 0;}
 source_busy=1;source_reentered=0;
 if(!source_owner()||source_reentered){if(source_session)source_fail(VSS_OWNER);source_busy=0;return 0;}
 return 1;
}
static int source_leave(int result){source_busy=0;return result;}
static int source_drained(uint64_t session,uint64_t epoch){
 /* Actual backend leases, including retired-but-unacknowledged tokens, must
  * be gone independently of the external consumer/fence drain authority. */
 for(unsigned i=0;i<8;i++)if(scene_wsi_tokens[i])return 0;
 return source_config.drained(source_config.user,session,epoch)==1&&!source_reentered;
}
int d3d8_vk_scene_source_configure(const VssConfig*c){
 if(source_configured||vk.ready||!c||!c->owner||!c->admitted||!c->drained||!c->failure)return 0;
 source_config=*c;source_configured=1;return 1;
}
int d3d8_vk_scene_source_begin(uint64_t session,uint64_t epoch){
 if(!source_configured||!source_enter())return 0;
 if(source_session||!session||session<=source_last||!epoch||epoch!=sn_device_generation||vk.frame_open||!scene_ready())return source_leave(0);
 if(source_config.admitted(source_config.user,session,epoch)!=1||!source_drained(session,epoch)||source_failure||source_reentered)return source_leave(0);
 source_session=source_last=session;source_epoch=epoch;source_failure=source_notified=0;return source_leave(1);
}
int d3d8_vk_scene_source_end(uint64_t session,uint64_t epoch){
 if(!source_configured||!source_enter())return 0;
 if(!source_session||session!=source_session||epoch!=source_epoch||epoch!=sn_device_generation||vk.frame_open)return source_leave(0);
 source_notify();
 if(source_reentered||!source_drained(session,epoch)||source_reentered)return source_leave(0);
 source_session=source_epoch=0;source_failure=source_notified=0;return source_leave(1);
}
int d3d8_vk_scene_source_validate(uint64_t session,uint64_t epoch){
 if(!source_configured||!source_session||session!=source_session||epoch!=source_epoch||source_busy)return 0;
 /* Transient recursion guard only; ordinary validation is not a new failure
  * event and never changes the active latch or its retirement authority. */
 source_busy=1;source_reentered=0;
 int valid=source_owner()&&!source_failure&&epoch==sn_device_generation&&
  source_config.admitted(source_config.user,session,epoch)==1&&scene_ready()&&
  !source_failure&&!source_reentered;
 return source_leave(valid);
}
/* Do NOT fold owner/admission/receipt success into this decision. A failed
 * active session is still source-only until its genuine drained cut. */
static int scene_source_active(void){return source_session!=0;}
static void scene_source_receipt(void){
 if(!source_session||!source_enter())return;
 if(source_epoch!=sn_device_generation)source_fail(VSS_EPOCH);
 if(!source_failure&&(source_config.admitted(source_config.user,source_session,source_epoch)!=1||!scene_ready()))source_fail(VSS_ADMISSION);
 if(!source_failure){
  D3D8SceneHostPolicy policy;
  if(!d3d8_vk_scene_host_policy(&policy))source_fail(VSS_POLICY);
  else if(!source_failure&&!source_reentered&&!vsj_backend_host_source_only(source_session,&policy))source_fail(VSS_RECEIPT);
 }
 source_notify();source_leave(1);
}
