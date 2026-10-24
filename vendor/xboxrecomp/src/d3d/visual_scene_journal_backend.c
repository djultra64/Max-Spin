/* No clocks, guest reads, original callbacks or GPU submissions. djultra64. */
#include "visual_scene_journal_backend.h"
static VsjBackendHooks hooks;
static uint32_t domain=VSJ_DOMAIN_GLOBAL;static int enabled,busy,faulting;
int vsj_backend_install(const VsjBackendHooks*h){
 if(enabled||!h||!h->event||!h->raw||!h->boundary||!h->fault)return 0;
 hooks=*h;enabled=1;return 1;
}
int vsj_backend_enabled(void){return enabled;}
uint32_t vsj_backend_domain(void){return domain;}
static void fault(void){if(faulting)return;faulting=1;hooks.fault(hooks.user,VSJ_UNKNOWN_WRITER);faulting=0;}
void vsj_backend_event(uint32_t kind,uint32_t phase,const void*args,size_t bytes,int result,const char*fn){
 if(!enabled)return;
 if(busy){fault();return;}
 if(kind<VSJ_PROGRAM_DRAW||kind>VSJ_FINAL_TARGET||phase<VSJ_PRE||phase>VSJ_HOST_SOURCE_ONLY){fault();return;}
 VsjBackendEvent e={VSJ_VERSION,kind,phase,domain,args,bytes,result,fn};
 busy=1;int ok=hooks.event(hooks.user,&e);busy=0;if(!ok)fault();
}
void vsj_backend_raw(uint32_t kind,const char*fn,unsigned line){
 if(!enabled)return;
 if(busy){fault();return;}
 if(kind<VSJ_RAW_DRAW||kind>VSJ_RAW_BLIT){fault();return;}
 busy=1;int ok=hooks.raw(hooks.user,kind,domain,fn,line);busy=0;if(!ok)fault();
}
void vsj_backend_boundary(uint32_t kind,uint64_t frame,uint32_t color){
 if(!enabled)return;
 if(busy){fault();return;}
 if(kind<VSJ_SCENE_ENTER||kind>VSJ_HOST_LEAVE){fault();return;}
 if(kind==VSJ_SCENE_ENTER)domain=VSJ_DOMAIN_SCENE;
 else if(kind==VSJ_HOST_ENTER)domain=VSJ_DOMAIN_HOST_PRESENT;
 busy=1;int ok=hooks.boundary(hooks.user,kind,frame,color);busy=0;if(!ok)fault();
 if(kind==VSJ_HOST_LEAVE)domain=VSJ_DOMAIN_GLOBAL;
}
void vsj_backend_program_source(const VfpSource*source){
 vsj_backend_event(VSJ_PROGRAM_DRAW,VSJ_SOURCE,source,source?sizeof*source:0,0,"actual_pb_program_source");
}

void vsj_backend_untracked(void){if(enabled)fault();}

int vsj_backend_host_source_only(uint64_t session,const D3D8SceneHostPolicy*p){
 if(!enabled)return 0;
 if(busy||domain!=VSJ_DOMAIN_HOST_PRESENT||!session||!p||p->version!=1||p->bytes!=sizeof*p){fault();return 0;}
 VsjHostSourceOnlyArguments a={1,sizeof a,session,*p};
 VsjBackendEvent e={VSJ_VERSION,VSJ_FINAL_TARGET,VSJ_HOST_SOURCE_ONLY,domain,&a,sizeof a,1,"actual_host_source_only"};
 busy=1;int ok=hooks.event(hooks.user,&e);busy=0;if(!ok)fault();return ok;
}
