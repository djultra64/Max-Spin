/* Single executor owner; immutable detached frames. Own work djultra64. */
#include "visual_scene_journal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct {VsjUse source;void*lease;} Retained;
typedef struct {VsjUse current;uint32_t initial,written;} Target;
typedef struct Node {VsjCommand c;VsjDependency dependencies[VSJ_MAX_USES];unsigned char bytes[];} Node;
struct VsjFrame {
 VsjRecorder*owner;VsjStamp stamp;uint32_t commands,retained,targets;size_t bytes;
 Node*nodes[VSJ_MAX_COMMANDS];Retained leases[VSJ_MAX_RESOURCES];Target target[VSJ_MAX_RESOURCES];
};
struct VsjRecorder {
 VsjConfig config;VsjFrame*active;size_t charged;uint32_t live,status,busy;
 uint64_t nonce,device_epoch,pending;uint32_t kind,count,writers;
 VsjUse before[VSJ_MAX_USES];uint32_t lease[VSJ_MAX_USES],target[VSJ_MAX_USES];
};
static int owned(const VsjRecorder*j){return j&&j->config.owner(j->config.user);}
static int samegen(VfpGeneration a,VfpGeneration b){return a.scene==b.scene&&a.view==b.view;}
static int generation(const VsjFrame*f){
 if(!f||f->owner->busy)return 0;
 VsjRecorder*j=f->owner;VfpGeneration g;j->busy=1;
 int ok=j->config.resources.generation(j->config.resources.user,&g);j->busy=0;
 return ok&&samegen(g,f->stamp.continuity)&&f->stamp.device_epoch==j->device_epoch;
}
static int shape(const VsjUse*a,const VsjUse*b){return a->resource.identity==b->resource.identity&&a->resource.format==b->resource.format&&a->resource.width==b->resource.width&&a->resource.height==b->resource.height&&a->resource.pitch==b->resource.pitch&&a->guest_epoch==b->guest_epoch;}
static int exact(const VsjUse*a,const VsjUse*b){return shape(a,b)&&a->resource.version==b->resource.version;}
static int validuse(const VsjUse*u){return u&&u->resource.role<VFP_MAX_RESOURCES&&u->resource.identity&&u->resource.version&&u->resource.width&&u->resource.height&&u->resource.pitch&&(u->access==VSJ_INPUT||u->access==VSJ_TARGET);}
void vsj_fault(VsjRecorder*j,uint32_t reason){if(owned(j)&&!j->status)j->status=reason?reason:VSJ_INVALID;}
static int ready(VsjRecorder*j){if(!owned(j)||j->busy||!j->active||j->status)return 0;if(!generation(j->active)){vsj_fault(j,VSJ_GENERATION);return 0;}return 1;}
static void*allocate(VsjRecorder*j,size_t bytes){if(bytes>j->config.max_bytes-j->charged){vsj_fault(j,VSJ_BUDGET);return NULL;}void*p=calloc(1,bytes);if(!p){vsj_fault(j,VSJ_BUDGET);return NULL;}j->charged+=bytes;return p;}
static void freeframe(VsjFrame*f){VsjRecorder*j=f->owner;j->busy=1;for(unsigned i=0;i<f->retained;i++)j->config.resources.release(j->config.resources.user,&f->leases[i].source.resource,f->leases[i].lease);for(unsigned i=0;i<f->commands;i++)free(f->nodes[i]);j->charged-=f->bytes;free(f);j->live--;j->busy=0;}
VsjRecorder*vsj_create(const VsjConfig*c){
 if(!c||!c->owner||!c->resources.generation||!c->resources.retain||!c->resources.validate||!c->resources.release||!c->owner(c->user)||c->max_bytes>VSJ_MAX_BYTES||c->max_bytes<sizeof(VsjRecorder)+sizeof(VsjFrame)||!c->max_commands||c->max_commands>VSJ_MAX_COMMANDS||!c->max_live_frames||c->max_live_frames>3)return NULL;
 VsjRecorder*j=calloc(1,sizeof*j);if(j){j->config=*c;j->charged=sizeof*j;}return j;
}
int vsj_begin(VsjRecorder*j,const VsjStamp*s){
 if(!owned(j)||j->busy||j->active||j->live==j->config.max_live_frames||!s||!s->device_epoch||!s->continuity.scene||!s->continuity.view)return 0;
 VfpGeneration g;j->busy=1;int generated=j->config.resources.generation(j->config.resources.user,&g);j->busy=0;if(!generated||!samegen(g,s->continuity))return 0;
 j->status=0;j->pending=0;j->device_epoch=s->device_epoch;VsjFrame*f=allocate(j,sizeof*f);if(!f)return 0;
 f->owner=j;f->stamp=*s;f->bytes=sizeof*f;j->active=f;j->live++;return 1;
}
static uint32_t retained(VsjRecorder*j,const VsjUse*u){
 VsjFrame*f=j->active;for(unsigned i=0;i<f->retained;i++)if(exact(&f->leases[i].source,u))return i;
 if(f->retained==VSJ_MAX_RESOURCES){vsj_fault(j,VSJ_BUDGET);return UINT32_MAX;}
 void*lease=NULL;j->busy=1;int ok=j->config.resources.retain(j->config.resources.user,&u->resource,&lease);j->busy=0;
 if(!ok||!lease){vsj_fault(j,VSJ_RESOURCE);return UINT32_MAX;}
 uint32_t index=f->retained++;f->leases[index]=(Retained){*u,lease};return index;
}
int vsj_owned_uses(VsjRecorder*j,const VsjUse*uses,uint32_t count){
 if(!ready(j)||j->pending||!uses||!count||count>VSJ_MAX_USES||j->nonce==UINT64_MAX)return 0;
 VsjFrame*f=j->active;unsigned targets=0;
 for(unsigned i=0;i<count;i++){
  const VsjUse*u=&uses[i];if(!validuse(u))return 0;
  for(unsigned k=0;k<i;k++)if(uses[k].resource.identity==u->resource.identity&&(uses[k].access==VSJ_TARGET||u->access==VSJ_TARGET))return 0;
  if(u->access==VSJ_TARGET)targets++;
  unsigned t=0;for(;t<f->targets;t++)if(f->target[t].current.resource.identity==u->resource.identity)break;
  unsigned lease=UINT32_MAX;
  if(t<f->targets){
   if(!exact(&f->target[t].current,u))return 0;
   if(!f->target[t].written)lease=f->target[t].initial;
  }else{
   for(unsigned k=0;k<f->retained;k++)if(exact(&f->leases[k].source,u)){lease=k;break;}
   if(lease==UINT32_MAX)return 0;
  }
  if(lease!=UINT32_MAX){
   if(lease>=f->retained)return 0;
   Retained*l=&f->leases[lease];j->busy=1;
   int ok=j->config.resources.validate(j->config.resources.user,&l->source.resource,l->lease);j->busy=0;
   if(!ok)return 0;
  }
 }
 return targets&&ready(j);
}
uint64_t vsj_prepare(VsjRecorder*j,uint32_t kind,const VsjUse*uses,uint32_t count){
 if(!ready(j))return 0;
 if(j->pending||kind<VSJ_PROGRAM_DRAW||kind>VSJ_SWAPCHAIN_BLIT||!uses||!count||count>VSJ_MAX_USES||j->nonce==UINT64_MAX){vsj_fault(j,VSJ_INVALID);return 0;}
 VsjFrame*f=j->active;unsigned targets=0;
 for(unsigned i=0;i<count;i++){
  const VsjUse*u=&uses[i];
  for(unsigned k=0;k<i;k++)if(uses[k].resource.identity==u->resource.identity&&(uses[k].access==VSJ_TARGET||u->access==VSJ_TARGET)){vsj_fault(j,VSJ_RESOURCE);return 0;}
  if(!validuse(u)){vsj_fault(j,VSJ_RESOURCE);return 0;}
  uint32_t t=0;for(;t<f->targets;t++)if(f->target[t].current.resource.identity==u->resource.identity)break;
  j->target[i]=UINT32_MAX;j->lease[i]=UINT32_MAX;
  if(t<f->targets){
   if(!exact(&f->target[t].current,u)){vsj_fault(j,VSJ_POSTWRITE);return 0;}
   if(u->access==VSJ_TARGET){j->target[i]=t;if(!f->target[t].written)j->lease[i]=f->target[t].initial;}
   else if(f->target[t].written)j->target[i]=t;
   else j->lease[i]=f->target[t].initial; /* canceled prepare never produced */
  }else{
   uint32_t lease=retained(j,u);if(lease==UINT32_MAX)return 0;j->lease[i]=lease;
   if(u->access==VSJ_TARGET){
    if(f->targets==VSJ_MAX_RESOURCES){vsj_fault(j,VSJ_BUDGET);return 0;}
    t=f->targets++;f->target[t]=(Target){.current=*u,.initial=lease};j->target[i]=t;
   }
  }
  if(u->access==VSJ_TARGET){targets++;for(unsigned k=0;k<i;k++)if(uses[k].access==VSJ_TARGET&&uses[k].resource.identity==u->resource.identity){vsj_fault(j,VSJ_INVALID);return 0;}}
  j->before[i]=*u;
 }
 if(!ready(j))return 0;
 if(!targets){vsj_fault(j,VSJ_RESOURCE);return 0;}
 j->kind=kind;j->count=count;j->writers=0;j->pending=++j->nonce;return j->pending;
}
void vsj_writer(VsjRecorder*j,uint32_t kind){
 if(!j||!j->active)return;
 if(!ready(j))return;
 if(!j->pending||kind!=j->kind||j->writers==UINT32_MAX){vsj_fault(j,VSJ_UNKNOWN_WRITER);return;}
 j->writers++;
}
int vsj_cancel(VsjRecorder*j,uint64_t ticket){
 if(!ready(j))return 0;
 if(!j->pending||ticket!=j->pending||j->writers){vsj_fault(j,VSJ_PENDING);return 0;}
 j->pending=0;return 1;
}
static Node*node(VsjRecorder*j,uint32_t kind,const VsjChunk*chunks,uint32_t count){
 if(j->active->commands==j->config.max_commands){vsj_fault(j,VSJ_BUDGET);return NULL;}
 if(!chunks||!count||count>VSJ_MAX_CHUNKS){vsj_fault(j,VSJ_PAYLOAD);return NULL;}
 size_t bytes=(16-sizeof(Node)%16)%16;unsigned mask=0;
 for(unsigned i=0;i<count;i++){
  const VsjChunk*c=&chunks[i];if(!c->encoding||c->encoding>VSJ_CHUNK_CPU_UPLOAD||!c->data||!c->bytes||c->bytes>VSJ_MAX_BYTES||mask&(1u<<c->encoding)||bytes>VSJ_MAX_BYTES-c->bytes){vsj_fault(j,VSJ_PAYLOAD);return NULL;}
  mask|=1u<<c->encoding;bytes=(bytes+c->bytes+15u)&~(size_t)15u;
 }
 unsigned required=1u<<VSJ_CHUNK_NATIVE_ARGS;
 if(kind==VSJ_PROGRAM_DRAW)required|=(1u<<VSJ_CHUNK_GEOMETRY)|(1u<<VSJ_CHUNK_INDICES)|(1u<<VSJ_CHUNK_PROGRAM)|(1u<<VSJ_CHUNK_CONSTANTS)|(1u<<VSJ_CHUNK_MATERIAL);
 if(kind==VSJ_UP_DRAW)required|=(1u<<VSJ_CHUNK_GEOMETRY)|(1u<<VSJ_CHUNK_MATERIAL);
 if(kind==VSJ_UPLOAD)required|=1u<<VSJ_CHUNK_CPU_UPLOAD;
 if((mask&required)!=required){vsj_fault(j,VSJ_PAYLOAD);return NULL;}
 Node*n=allocate(j,sizeof*n+bytes);if(!n)return NULL;
 n->c.kind=kind;n->c.serial=j->active->commands+1;n->c.chunk_count=count;n->c.payload=n->bytes;n->c.payload_bytes=bytes;
 size_t offset=(16-sizeof(Node)%16)%16;
 for(unsigned i=0;i<count;i++){n->c.chunks[i]=(VsjOwnedChunk){chunks[i].encoding,(uint32_t)offset,(uint32_t)chunks[i].bytes};memcpy(n->bytes+offset,chunks[i].data,chunks[i].bytes);offset=(offset+chunks[i].bytes+15u)&~(size_t)15u;}
 j->active->bytes+=sizeof*n+bytes;return n;
}
int vsj_commit(VsjRecorder*j,uint64_t ticket,const VsjUse*after,uint32_t count,const VsjChunk*chunks,uint32_t nchunks){
 if(!ready(j))return 0;
 if(!j->pending||ticket!=j->pending||count!=j->count||!after){vsj_fault(j,VSJ_PENDING);return 0;}
 if(!j->writers){vsj_fault(j,VSJ_MISSING_WRITER);return 0;}
 for(unsigned i=0;i<count;i++){
  if(!validuse(&after[i])||after[i].access!=j->before[i].access||after[i].resource.role!=j->before[i].resource.role||!shape(&j->before[i],&after[i])||(after[i].access==VSJ_INPUT?after[i].resource.version!=j->before[i].resource.version:after[i].resource.version<=j->before[i].resource.version)){vsj_fault(j,VSJ_RESOURCE);return 0;}
 }
 Node*n=node(j,j->kind,chunks,nchunks);if(!n)return 0;n->c.ticket=ticket;n->c.writer_count=j->writers;n->c.use_count=count;
 uint32_t ci=j->active->commands;
 for(unsigned i=0;i<count;i++){
  uint32_t t=j->target[i];n->c.use_indices[i]=ci*VSJ_MAX_USES+i;
  n->dependencies[i]=(VsjDependency){j->before[i],after[i],j->lease[i],j->before[i].access==VSJ_INPUT&&t!=UINT32_MAX};
  if(after[i].access==VSJ_TARGET){j->active->target[t].current=after[i];j->active->target[t].written=1;}
 }
 j->active->nodes[j->active->commands++]=n;j->pending=0;return 1;
}
int vsj_state(VsjRecorder*j,uint32_t kind,const VsjChunk*chunks,uint32_t count){
 if(!ready(j))return 0;
 if(j->pending||kind<VSJ_TARGET_SELECT||kind>VSJ_FINAL_TARGET){vsj_fault(j,VSJ_PENDING);return 0;}
 Node*n=node(j,kind,chunks,count);if(!n)return 0;j->active->nodes[j->active->commands++]=n;return 1;
}
int vsj_seal(VsjRecorder*j,uint64_t identity,VsjFrame**out){
 if(!owned(j)||j->busy||!out||*out||!j->active)return 0;
 VsjFrame*f=j->active;
 if(!j->status&&!ready(j)){} /* latches a changed continuity */
 if(!j->status&&(j->pending||!f->commands))vsj_fault(j,VSJ_PENDING);
 if(!j->status){
  const VsjCommand*c=&f->nodes[f->commands-1]->c;int final=0;
  if(c->kind==VSJ_FINAL_TARGET)for(unsigned i=0;i<c->chunk_count;i++)if(c->chunks[i].encoding==VSJ_CHUNK_NATIVE_ARGS&&c->chunks[i].bytes==16){uint32_t v;uint64_t target;memcpy(&v,c->payload+c->chunks[i].offset,4);memcpy(&target,c->payload+c->chunks[i].offset+8,8);final=v==1&&target==identity;}
  int known=0;for(unsigned i=0;i<f->targets;i++)if(f->target[i].current.resource.identity==identity)known=1;
  if(!final||!known)vsj_fault(j,VSJ_RESOURCE);
 }
 j->active=NULL;j->pending=0;
 if(j->status){freeframe(f);return 0;}
 *out=f;return 1;
}
uint32_t vsj_status(const VsjRecorder*j){return j?j->status:VSJ_INVALID;}
size_t vsj_charged_bytes(const VsjRecorder*j){return j?j->charged:0;}
const VsjStamp*vsj_stamp(const VsjFrame*f){return f?&f->stamp:NULL;}
uint32_t vsj_command_count(const VsjFrame*f){return f?f->commands:0;}
const VsjCommand*vsj_command(const VsjFrame*f,uint32_t i){return f&&i<f->commands?&f->nodes[i]->c:NULL;}
const VsjDependency*vsj_dependency(const VsjFrame*f,uint32_t i){const VsjCommand*c=vsj_command(f,i/VSJ_MAX_USES);return c&&i%VSJ_MAX_USES<c->use_count?&f->nodes[i/VSJ_MAX_USES]->dependencies[i%VSJ_MAX_USES]:NULL;}
int vsj_validate(const VsjFrame*f){
 if(!f||!owned(f->owner)||f->owner->busy||!generation(f))return 0;
 VsjRecorder*j=f->owner;VfpProvider*p=&j->config.resources;j->busy=1;int ok=1;for(unsigned i=0;i<f->retained;i++)if(!p->validate(p->user,&f->leases[i].source.resource,f->leases[i].lease)){ok=0;break;}j->busy=0;return ok;
}
int vsj_retained(const VsjFrame*f,uint32_t i,VsjUse*out,void**lease){if(!f||!out||!lease||i>=f->retained||!vsj_validate(f))return 0;*out=f->leases[i].source;*lease=f->leases[i].lease;return 1;}
int vsj_release(VsjFrame**frame){if(!frame||!*frame)return 1;VsjFrame*f=*frame;if(!owned(f->owner)||f->owner->busy||f->owner->active==f)return 0;freeframe(f);*frame=NULL;return 1;}
int vsj_destroy(VsjRecorder**ptr){if(!ptr||!*ptr)return 1;VsjRecorder*j=*ptr;if(!owned(j)||j->busy||j->active||j->live)return 0;free(j);*ptr=NULL;return 1;}
