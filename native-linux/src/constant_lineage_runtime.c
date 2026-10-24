#include "constant_lineage_runtime.h"
#include "visual_pb_hooks.h"
#include "nv2a_vsh_interp.h"
#include "nv2a_executor_job.h"
static VclLedger ledger;
static VsaArchive*archive;
static int initialized;
static int owner(void*u){(void)u;return nv2a_executor_is_owner();}
static void method(void*u,const VpliPacket*p,uint32_t word){
 (void)u;if(!owner(NULL))return;
 if(!p){vcl_invalidate(&ledger);return;}
 (void)vcl_method(&ledger,p->method,word,p);
}
static void written(void*u,uint32_t row,uint32_t lane,uint32_t word,uint32_t revision){
 (void)u;if(owner(NULL))(void)vcl_written(&ledger,row,lane,word,revision);
}
static void unknown(void*u,uint32_t first,uint32_t rows){
 (void)u;(void)first;(void)rows;
 /* Full revoke: a raw store advances the original global revision and may
  * have changed a cursor outside consumed-PB provenance. */
 if(owner(NULL))vcl_invalidate(&ledger);
}
int topspin_constant_lineage_init(void){
 if(initialized)return 0;
 if(!vcl_configure(&ledger,1,NULL,owner))return 0;
 Nv2aVshConstantObserver observer={NULL,written,unknown};
 if(!nv2a_vsh_observe_constants(&observer)||!vpli_method_observer_install(method,NULL))return 0;
 initialized=1;return 1;
}
int topspin_constant_lineage_capture(const VpliPacket*p,const VfpSource*s,uint64_t epoch){
 if(!initialized||!owner(NULL)||!p||!s||!epoch)return VSA_INVALID;
 VclSnapshot proof;
 if(!vcl_snapshot(&ledger,s->constant_revision,&proof))return VSA_MISSING;
 if(!archive){VsaConfig c={NULL,owner,VSA_MAX_RECORDS,VSA_MAX_BYTES};archive=vsa_create(&c);if(!archive)return VSA_BUDGET;}
 return vsa_capture(archive,p,s,&proof,vcl_snapshot_bytes(&proof),epoch);
}
int topspin_constant_lineage_lookup(const VsjFrame*f,uint32_t index,const VclSnapshot**out){
 if(out)*out=NULL;
 if(!initialized||!owner(NULL)||!archive)return VSA_MISSING;
 return vsa_lookup(archive,f,index,out);
}
void topspin_constant_lineage_retire(uint64_t frame){
 if(initialized&&owner(NULL)&&archive)(void)vsa_retire(archive,frame);
}
