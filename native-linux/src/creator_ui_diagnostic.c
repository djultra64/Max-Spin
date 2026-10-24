/* Read-only bounded UI evidence; no forced Init or control changes. djultra64. */
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "creator_ui_diagnostic.h"
#include "actor_binding_cpu_guard.h"
#include "material_coherence_bridge.h"
#include "material_tracking_diagnostic.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
 uint32_t va,vptr,active,raw208,focus_slot,focus,focus_owner,raw_header[4];
 char name[64],focus_name[64];
} Scene;
typedef struct {
 uint32_t system,global_context,context,manager,global_manager,context_owner,context_hub;
 uint32_t global_active,global_active_owner,global_active_class;
 char global_active_name[64];Scene loading,current,hub;
} State;
static _Atomic unsigned emitted;
static atomic_flag material_logged=ATOMIC_FLAG_INIT;
static atomic_flag tracking_logged=ATOMIC_FLAG_INIT;
static _Thread_local int busy,have_previous;
static _Thread_local State previous;
static void*ram(uint32_t at,size_t n){return at>=0x10000u&&(uint64_t)at+n<=0x4000000u?(void*)XBOX_PTR(at):NULL;}
static uint32_t word(uint32_t at){uint32_t value=0;void*p=ram(at,4);if(p)memcpy(&value,p,4);return value;}
static uint32_t byte(uint32_t at){unsigned char*p=ram(at,1);return p?*p:0x100u;}
static void text(uint32_t ptr,char out[64]){
 strcpy(out,"<unavailable>");if(ptr<0x1000cu)return;
 uint32_t n=word(ptr-12);if(!n||n>63||!ram(ptr,(size_t)n+1))return;
 const unsigned char*src=ram(ptr,(size_t)n+1);if(src[n])return;
 for(uint32_t i=0;i<n;i++)out[i]=src[i]>=32&&src[i]<=126&&src[i]!='"'?(char)src[i]:'?';out[n]=0;
}
static void scene(uint32_t at,Scene*out){
 memset(out,0,sizeof*out);out->va=at;out->active=out->raw208=0x100;
 strcpy(out->name,"<unavailable>");strcpy(out->focus_name,"<unavailable>");
 if(!ram(at,0x209))return;
 out->vptr=word(at);out->active=byte(at+0x19c);out->raw208=byte(at+0x208);text(word(at+0x18c),out->name);
 /* Original268E0: scene+174 holds the shared-pointer slot for active control.
  * Read without invoking the getter or changing its reference count. */
 out->focus_slot=word(at+0x174);out->focus=word(out->focus_slot);
 if(ram(out->focus,0x10)){out->focus_owner=word(out->focus+0xc);text(word(out->focus+8),out->focus_name);}
 memcpy(out->raw_header,ram(at,sizeof out->raw_header),sizeof out->raw_header);
}
void topspin_creator_ui_observe(uint32_t loading,uint32_t context,uint32_t manager,uint32_t hub){
 if(busy)return;
 VabGuestCpu cpu;vab_cpu_save(&cpu);const char*flag=getenv("TOPSPIN_CREATOR_UI_DIAGNOSTICS");
 if(!flag||strcmp(flag,"1"))goto done;
 busy=1;
 /* ready() is the existing acquire-publication check. The diagnostic is
  * immutable HOST-owned memory afterwards; never call/query the GPU here. */
 if(topspin_material_bridge_first_failure_ready()&&!atomic_flag_test_and_set_explicit(&material_logged,memory_order_relaxed)){
  McBridgeDiagnostic d=topspin_material_bridge_first_failure;
  fprintf(stderr,"[CREATOR-UI-MATERIAL] first_host_rejection version=%u stage=%u resource=%08x parent=%08x mip=%u width=%u height=%u invoke_status=%d callback_result=%d lock_equal=%d gpu_version=%u gpu_stage=%u gpu_classify=%d gpu_backend=%d rjp_version=%u rjp_broken=%u rjp_tracking_fault=%u rjp_retired=%u/%u rjp_live=%u rjp_related=%u rjp_same=%u rjp_writes=%u rjp_color_writes=%u; existing diagnostic only, no new query\n",d.version,d.stage,d.resource,d.parent,d.level,d.width,d.height,d.invoke_status,d.callback_result,d.lock_equal,d.gpu.version,d.gpu.stage,d.gpu.classify,d.gpu.backend_result,d.gpu.rjp.version,d.gpu.rjp.broken,d.gpu.rjp.tracking_fault,d.gpu.rjp.retired_resource,d.gpu.rjp.retired_parent,d.gpu.rjp.live_objects,d.gpu.rjp.related_objects,d.gpu.rjp.same_objects,d.gpu.rjp.writes_used,d.gpu.rjp.color_writes);
 }
 if(topspin_material_first_failure_ready()&&!atomic_flag_test_and_set_explicit(&tracking_logged,memory_order_relaxed)){
  McTrackingFailure d=topspin_material_first_failure;const RjpRegister*q=&d.registration;
  fprintf(stderr,"[CREATOR-UI-TRACKING] first_host_failure version=%u stage=%u operation=%u entry=%08x end_esp=%08x device=%08x observed_device=%08x begin=%08x end=%08x base=%08x size=%u after_base=%08x after_size=%u epoch=%llu after_epoch=%llu invoke_status=%d invoke_value=%d checkpoint_valid=%u checkpoint_version=%u checkpoint_serial=%llu registered_status=%u registered_guest_id=%llu registration_version=%u resource=%08x parent=%08x format=%u width=%u height=%u pitch=%u resolved=%08x guest_epoch=%llu ring=%08x/%08x span=%08x/%08x wrap_tail=%08x registration_checkpoint=%llu header=%08x,%08x,%08x,%08x,%08x,%08x parent_header=%08x,%08x,%08x,%08x,%08x; immutable existing record only\n",
   d.version,d.stage,d.operation,d.entry,d.end_esp,d.device,d.observed_device,d.begin,d.end,d.base,d.size,d.after_base,d.after_size,
   (unsigned long long)d.epoch,(unsigned long long)d.after_epoch,d.invoke_status,d.invoke_value,d.checkpoint_valid,d.checkpoint.version,(unsigned long long)d.checkpoint.serial,d.registered.status,(unsigned long long)d.registered.guest_id,
   q->version,q->resource,q->parent,q->format,q->width,q->height,q->pitch,q->resolved_data,(unsigned long long)q->guest_epoch,q->ring_base,q->ring_limit,q->begin,q->end,q->wrap_tail_end,(unsigned long long)q->checkpoint,
   q->header[0],q->header[1],q->header[2],q->header[3],q->header[4],q->header[5],q->parent_header[0],q->parent_header[1],q->parent_header[2],q->parent_header[3],q->parent_header[4]);
 }
 if(atomic_load_explicit(&emitted,memory_order_relaxed)>=12)goto observed;
 State state;memset(&state,0,sizeof state);
 state.system=word(0x2b7524);state.global_context=word(0x2b7768);state.context=context;state.manager=manager;state.global_manager=word(0x390a5c);
 if(ram(context,0x10)){state.context_owner=word(context+8);state.context_hub=word(context+0xc);}
 uint32_t current=ram(state.system,0x1248)?word(state.system+0x1244):0;
 scene(loading,&state.loading);scene(current,&state.current);scene(hub,&state.hub);
 /* Original96030/96193 updates global active control2B8B20. It is separate
  * evidence from the current scene's focus slot, never substituted for it. */
 state.global_active=word(0x2b8b20);strcpy(state.global_active_name,"<unavailable>");
 if(ram(state.global_active,0x10)){state.global_active_class=word(state.global_active);state.global_active_owner=word(state.global_active+0xc);text(word(state.global_active+8),state.global_active_name);}
 State key=state;memset(key.loading.raw_header,0,sizeof key.loading.raw_header);memset(key.current.raw_header,0,sizeof key.current.raw_header);memset(key.hub.raw_header,0,sizeof key.hub.raw_header);
 if(have_previous&&!memcmp(&key,&previous,sizeof key))goto observed;
 previous=key;have_previous=1;unsigned seq=atomic_fetch_add_explicit(&emitted,1,memory_order_relaxed);if(seq>=12)goto observed;
 fprintf(stderr,"[CREATOR-UI] seq=%u system=%08x current=%08x name=\"%s\" active19c=%x raw208=%x class=%08x focus_slot=%08x focus=%08x focus_owner_raw=%08x focus_name_raw=\"%s\" global_active=%08x global_active_class_raw=%08x global_active_owner_raw=%08x global_active_name_raw=\"%s\" context_arg=%08x context_global=%08x context_owner=%08x manager_arg=%08x manager_global=%08x context_hub=%08x loading=%08x loading_name=\"%s\" loading_active19c=%x loading_complete208=%x hub_arg=%08x hub_name=\"%s\" hub_active19c=%x hub_raw208=%x hub_focus=%08x hub_focus_name_raw=\"%s\" current_header_RAW_00_0c=%08x,%08x,%08x,%08x\n",
  seq+1,state.system,state.current.va,state.current.name,state.current.active,state.current.raw208,state.current.vptr,state.current.focus_slot,state.current.focus,state.current.focus_owner,state.current.focus_name,
  state.global_active,state.global_active_class,state.global_active_owner,state.global_active_name,state.context,state.global_context,state.context_owner,state.manager,state.global_manager,state.context_hub,
  state.loading.va,state.loading.name,state.loading.active,state.loading.raw208,state.hub.va,state.hub.name,state.hub.active,state.hub.raw208,state.hub.focus,state.hub.focus_name,
  state.current.raw_header[0],state.current.raw_header[1],state.current.raw_header[2],state.current.raw_header[3]);
observed:busy=0;
done:vab_cpu_restore(&cpu);
}
