/* Real ordered writer recorder. Own work djultra64. No GPU/guest dispatch. */
#include "visual_scene_recorder.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct VsrRecorder {
 VsrSourceOnlyPublish source_only_publish;void*source_only_user;VsjHostSourceOnlyArguments source_only;unsigned source_only_seen;
 VsrConfig c;VfpProvider provider;VsjRecorder*j;VsrStats stats;VsjStamp stamp;VsrSourceTag source;VsrHostPolicy host;
 VsjUse before[6],after[6];unsigned count,flags,active,bad,target_active,waiting,raw,kind;
 uint64_t ticket,final,last_attempt;unsigned attempted;uint32_t completed,completed_seen,host_entered;VsrDrawNative draw;
 unsigned char material[VSR_MAX_MATERIAL];uint32_t material_bytes;
 unsigned char*scratch;size_t scratch_cap,scratch_bytes;
 VsjGeometryArguments geometry;unsigned geometry_ready;VsrUploadNative upload;uint64_t upload_image;unsigned upload_ready,upload_writes,gamma_pending,gamma_raw;
 float gamma[256][4];unsigned gamma_enabled,policy_seen,discard_source;VsrDrawNative discarded;
};
static int fail(VsrRecorder*r,unsigned reason){if(!reason)reason=VSJ_INVALID;if(!r->bad){r->bad=1;r->stats.rejected++;r->c.invalidate(r->c.user,reason);}if(r->j)vsj_fault(r->j,reason);return 0;}
static void fault(void*u,uint32_t reason){fail(u,reason);}
static int own(VsrRecorder*r){return r->c.owner(r->c.user);}
static int current_matches(VsrRecorder*r){
 if(!r->active||r->bad)return 1;VsjStamp s;VsrSourceTag tag;memset(&s,0,sizeof s);memset(&tag,0,sizeof tag);
 if(!r->c.source_tag(r->c.user,&tag)||memcmp(&tag,&r->source,sizeof tag))return fail(r,VSJ_GENERATION);
 if(!r->c.current(r->c.user,r->stamp.frame_serial,&s)||s.frame_serial!=r->stamp.frame_serial||s.simulation_tick!=r->stamp.simulation_tick||s.continuity.scene!=r->stamp.continuity.scene||s.continuity.view!=r->stamp.continuity.view||(s.device_epoch&&s.device_epoch!=r->stamp.device_epoch))return fail(r,VSJ_GENERATION);return 1;
}
static int uses(VsrRecorder*r,unsigned flags,VsjUse*out,unsigned*n){
 VfpResource a[6];unsigned k=0;if(!r->c.bindings(r->c.user,flags,a,6,&k)||!k||k>6)return 0;
 for(unsigned i=0;i<k;i++)out[i]=(VsjUse){a[i],r->stamp.device_epoch,a[i].role<2?VSJ_TARGET:VSJ_INPUT};*n=k;return 1;
}
static int start(VsrRecorder*r,unsigned kind,unsigned flags){
 if(r->bad)return 1;if(r->ticket||r->waiting||r->upload_ready||!uses(r,flags,r->before,&r->count))return fail(r,VSJ_RESOURCE);
 /* Only exact already-owned versions can skip the outer safe flush. Querying
  * bindings is CPU registry metadata; a miss preserves flush+fresh requery. */
 if(!vsj_owned_uses(r->j,r->before,r->count)&&(!r->c.prepare(r->c.user)||!uses(r,flags,r->before,&r->count)))return fail(r,VSJ_RESOURCE);
 r->ticket=vsj_prepare(r->j,kind,r->before,r->count);if(!r->ticket)return fail(r,vsj_status(r->j));r->kind=kind;r->flags=flags;r->raw=0;r->geometry_ready=0;return 1;
}
static int commit(VsrRecorder*r,const VsjChunk*c,unsigned n){
 if(!vsj_commit(r->j,r->ticket,r->after,r->count,c,n))return fail(r,vsj_status(r->j));r->ticket=0;r->waiting=0;r->raw=0;return 1;
}
static int cancel(VsrRecorder*r){if(!vsj_cancel(r->j,r->ticket))return fail(r,VSJ_MISSING_WRITER);r->ticket=0;r->waiting=0;return 1;}
static int post(VsrRecorder*r,unsigned flags){unsigned n=0;return uses(r,flags,r->after,&n)&&n==r->count;}
static int material(VsrRecorder*r){r->material_bytes=0;return r->c.material(r->c.user,r->material,sizeof r->material,&r->material_bytes)&&r->material_bytes&&r->material_bytes<=sizeof r->material&&r->c.view(r->c.user,&r->draw.view);}
static int source(VsrRecorder*r,const VfpSource*s){
 if(r->bad)return 1;
 if(!r->waiting&&r->discard_source){r->discard_source=0;if(!s||s->version!=VFP_VERSION||s->original_frame!=r->stamp.frame_serial||s->used!=r->discarded.used||s->nvertices!=r->discarded.vertex_count||s->nindices!=r->discarded.index_count||s->program_hash!=r->discarded.program_hash||s->constant_revision!=r->discarded.constant_revision)return fail(r,VSJ_PAYLOAD);return 1;}
 unsigned per=0;if(s)for(unsigned i=0;i<16;i++)per+=(s->used>>i)&1u;if(!per)per=1;
 if(!r->waiting||!s||s->version!=VFP_VERSION||s->original_frame!=r->stamp.frame_serial||s->used!=r->draw.used||s->nvertices!=r->draw.vertex_count||s->nindices!=r->draw.index_count||s->program_hash!=r->draw.program_hash||s->constant_revision!=r->draw.constant_revision||!s->program_revision||s->program_start>=136||(s->primitive!=1&&s->primitive!=5)||s->camera.encoding!=VFP_CAMERA_BAKED_NV2A||!s->vertices||!s->indices||!s->program||!s->constants||s->program_bytes!=136u*16||s->constant_bytes!=192u*16||(s->used&0xffff0000u)||s->vertex_bytes!=(size_t)s->nvertices*per*16||s->index_bytes!=(size_t)s->nindices*2)return fail(r,VSJ_PAYLOAD);
 if(s->primitive!=1)for(uint32_t i=0;i<s->nindices;i++)if(s->indices[i]>=s->nvertices)return fail(r,VSJ_PAYLOAD);
 r->draw.geometry_encoding=VSR_GEOMETRY_PROGRAM_PACKED;r->draw.emitted_vertex_stride=per*16;r->draw.emitted_vertex_count=s->primitive==1?s->nindices:s->nvertices;r->draw.vertex_stride=per*16;r->draw.primitive=s->primitive;r->draw.program_revision=s->program_revision;r->draw.program_start=s->program_start;r->draw.camera=s->camera;
 VsjChunk c[]={{VSJ_CHUNK_GEOMETRY,s->vertices,s->vertex_bytes},{VSJ_CHUNK_INDICES,s->indices,s->index_bytes},{VSJ_CHUNK_PROGRAM,s->program,s->program_bytes},{VSJ_CHUNK_CONSTANTS,s->constants,s->constant_bytes},{VSJ_CHUNK_MATERIAL,r->material,r->material_bytes},{VSJ_CHUNK_NATIVE_ARGS,&r->draw,sizeof r->draw}};return commit(r,c,6);
}
static int upload(VsrRecorder*r,const VsjBackendEvent*e){
 if(e->argument_bytes!=sizeof(VsjUploadArguments)||!e->arguments)return fail(r,VSJ_PAYLOAD);const VsjUploadArguments*a=e->arguments;
 if(e->phase==VSJ_PRE){
  if(r->upload_ready||r->ticket)return fail(r,VSJ_PENDING);
  VfpResource v;if(!r->c.upload(r->c.user,a->image,&v)||!v.version)return fail(r,VSJ_RESOURCE);
  r->before[0]=(VsjUse){v,r->stamp.device_epoch,VSJ_TARGET};r->count=1;
  if(!vsj_owned_uses(r->j,r->before,1)){
   if(!r->c.prepare(r->c.user)||!r->c.upload(r->c.user,a->image,&v)||!v.version)return fail(r,VSJ_RESOURCE);
   r->before[0]=(VsjUse){v,r->stamp.device_epoch,VSJ_TARGET};
  }
  r->ticket=vsj_prepare(r->j,VSJ_UPLOAD,r->before,1);if(!r->ticket)return fail(r,VSJ_RESOURCE);
  r->upload_image=a->image;r->upload_ready=1;r->upload_writes=0;r->kind=VSJ_UPLOAD;r->raw=0;return 1;
 }
 if(e->phase==VSJ_POST&&e->function&&(!strcmp(e->function,"tex_UnlockRect")||!strcmp(e->function,"cube_UnlockRect"))){
  if(!r->upload_ready||a->image!=r->upload_image)return fail(r,VSJ_RESOURCE);
  if(r->ticket){if(r->raw||r->upload_writes)return fail(r,VSJ_MISSING_WRITER);if(!cancel(r))return 0;}
  r->upload_ready=0;if(e->result!=0&&r->upload_writes)return fail(r,VSJ_PAYLOAD);return 1;
 }
 if(e->phase==VSJ_UPLOAD_BYTES){
  if(!r->upload_ready||a->image!=r->upload_image||!a->rgba||!a->width||!a->height||a->bytes!=(uint64_t)a->width*a->height*4||a->bytes>r->scratch_cap)return fail(r,VSJ_PAYLOAD);
  VfpResource v;if(!r->c.upload(r->c.user,a->image,&v)||!v.version)return fail(r,VSJ_RESOURCE);
  if(a->level>=32||a->face>5||a->width!=((v.width>>a->level)?(v.width>>a->level):1)||a->height!=((v.height>>a->level)?(v.height>>a->level):1)||(!strcmp(e->function,"upload_level")&&a->face))return fail(r,VSJ_PAYLOAD);
  if(!r->ticket){
   /* Only the exact already-produced target is allowed mid-scope. Its initial
    * lease is already owned; core must find its internal version edge. */
   if(!r->upload_writes||v.identity!=r->after[0].resource.identity||v.version!=r->after[0].resource.version)return fail(r,VSJ_RESOURCE);
   r->before[0]=(VsjUse){v,r->stamp.device_epoch,VSJ_TARGET};r->ticket=vsj_prepare(r->j,VSJ_UPLOAD,r->before,1);if(!r->ticket)return fail(r,VSJ_RESOURCE);
  }else if(v.identity!=r->before[0].resource.identity||v.version!=r->before[0].resource.version)return fail(r,VSJ_RESOURCE);
  memcpy(r->scratch,a->rgba,a->bytes);r->scratch_bytes=a->bytes;r->upload_image=a->image;r->kind=VSJ_UPLOAD;r->raw=0;r->upload=(VsrUploadNative){VSR_VERSION,a->width,a->height,a->level,a->face,!strcmp(e->function,"cube_upload"),(uint32_t)a->bytes,0};return 1;
 }
 if(e->phase==VSJ_POST){VfpResource v;if(!r->ticket||r->kind!=VSJ_UPLOAD||a->image!=r->upload_image||a->width!=r->upload.width||a->height!=r->upload.height||a->level!=r->upload.level||a->face!=r->upload.face||!r->raw||!r->c.upload(r->c.user,a->image,&v))return fail(r,VSJ_RESOURCE);r->after[0]=(VsjUse){v,r->stamp.device_epoch,VSJ_TARGET};VsjChunk c[]={{VSJ_CHUNK_CPU_UPLOAD,r->scratch,r->scratch_bytes},{VSJ_CHUNK_NATIVE_ARGS,&r->upload,sizeof r->upload}};int ok=commit(r,c,2);if(ok)r->upload_writes++;return ok;}return fail(r,VSJ_INVALID);
}
static int event(void*u,const VsjBackendEvent*e){
 VsrRecorder*r=u;if(!own(r)||!e||!e->function||e->version!=VSJ_VERSION)return fail(r,VSJ_OWNER);
 if(!current_matches(r))return 0;
 if(e->domain==VSJ_DOMAIN_HOST_PRESENT){
  if(!r->active||!r->host_entered)return fail(r,VSJ_GENERATION);
  if(e->kind==VSJ_FINAL_TARGET&&e->phase==VSJ_HOST_SOURCE_ONLY){
   if(!r->source_only_publish||r->source_only_seen||r->policy_seen||r->host.raw_gamma||r->gamma_pending||r->host.raw_swap_clears||r->host.raw_swap_blits||r->host.raw_copies!=(r->completed?1u:0u)||r->ticket||r->waiting||!r->final||e->result!=1||!e->function||strcmp(e->function,"actual_host_source_only")||!e->arguments||e->argument_bytes!=sizeof(VsjHostSourceOnlyArguments))return fail(r,VSJ_PAYLOAD);
   const VsjHostSourceOnlyArguments*a=e->arguments;D3D8SceneHostPolicy actual;
   memset(&actual,0,sizeof actual);
   if(a->version!=1||a->bytes!=sizeof*a||!a->session_id||a->policy.version!=1||a->policy.bytes!=sizeof a->policy||!r->c.host_policy(r->c.user,&actual)||memcmp(&actual,&a->policy,sizeof actual))return fail(r,VSJ_PAYLOAD);
   r->source_only=*a;r->source_only_seen=1;return 1;
  }
  if(r->source_only_seen)return fail(r,VSJ_UNKNOWN_WRITER);
  if(e->kind!=VSJ_GAMMA||e->argument_bytes!=sizeof(VsjGammaArguments)||!e->arguments)return fail(r,VSJ_UNKNOWN_WRITER);
  const VsjGammaArguments*a=e->arguments;
  if(e->phase==VSJ_PRE){if(r->gamma_pending||!a->ramp||a->ramp_bytes!=sizeof r->gamma)return fail(r,VSJ_PAYLOAD);memcpy(r->gamma,a->ramp,sizeof r->gamma);r->gamma_enabled=a->enabled;r->gamma_pending=1;r->gamma_raw=0;return 1;}
  if(e->phase==VSJ_POST){if(!r->gamma_pending||!!e->result!=!!r->gamma_raw)return fail(r,VSJ_MISSING_WRITER);r->host.gamma_applied=!!e->result;r->gamma_pending=0;return 1;}return fail(r,VSJ_UNKNOWN_WRITER);
 }
 if(e->kind==VSJ_TARGET_SELECT&&e->phase==VSJ_POST){if(e->result==1)r->target_active=e->arguments!=NULL;if(!r->active||r->bad)return 1;}
 if(!r->active)return e->kind==VSJ_UPLOAD||e->kind==VSJ_VIEW||e->kind==VSJ_TARGET_SELECT?1:fail(r,VSJ_UNKNOWN_WRITER);
 if(r->bad)return 1;
 if(e->kind==VSJ_PROGRAM_DRAW&&e->phase==VSJ_SOURCE)return e->argument_bytes==sizeof(VfpSource)?source(r,e->arguments):fail(r,VSJ_PAYLOAD);
 if(r->waiting)return fail(r,VSJ_PENDING);
 if(r->upload_ready&&e->kind!=VSJ_UPLOAD)return fail(r,VSJ_PENDING);
 if(e->kind==VSJ_UPLOAD)return upload(r,e);
 if(e->kind==VSJ_UP_DRAW&&e->phase==VSJ_GEOMETRY){
  if(!r->ticket||r->kind!=VSJ_UP_DRAW||r->geometry_ready||r->raw||!e->arguments||e->argument_bytes!=sizeof(VsjGeometryArguments))return fail(r,VSJ_PAYLOAD);
  const VsjGeometryArguments*a=e->arguments;
  if(!a->vertices||a->encoding!=VSJ_GEOMETRY_FIXED_GPU32||a->stride!=32||!a->vertex_count||a->bytes!=(uint64_t)a->vertex_count*32||a->bytes>r->scratch_cap)return fail(r,VSJ_PAYLOAD);
  memcpy(r->scratch,a->vertices,a->bytes);r->geometry=*a;r->geometry.vertices=NULL;r->geometry_ready=1;return 1;
 }
 if(e->kind==VSJ_PROGRAM_DRAW||e->kind==VSJ_UP_DRAW||e->kind==VSJ_CLEAR){
  unsigned flags=7;if(e->kind==VSJ_CLEAR){if(e->argument_bytes!=sizeof(VsjClearArguments)||!e->arguments)return fail(r,VSJ_PAYLOAD);const VsjClearArguments*a=e->arguments;flags=((a->flags&1)?1:0)|((a->flags&6)?2:0);}
  if(e->phase==VSJ_PRE)return start(r,e->kind,flags);
  if(e->phase!=VSJ_POST||!r->ticket||r->kind!=e->kind)return fail(r,VSJ_PENDING);
  if(!r->raw){if(e->kind==VSJ_UP_DRAW&&r->geometry_ready)return fail(r,VSJ_MISSING_WRITER);if(e->kind==VSJ_PROGRAM_DRAW){if(e->argument_bytes!=sizeof(VsjProgramArguments)||!e->arguments)return fail(r,VSJ_PAYLOAD);const VsjProgramArguments*a=e->arguments;r->discard_source=1;memset(&r->discarded,0,sizeof r->discarded);r->discarded.used=a->used;r->discarded.vertex_count=a->vertex_count;r->discarded.index_count=a->index_count;r->discarded.program_hash=a->hash;r->discarded.constant_revision=a->constant_revision;}return cancel(r);}
  if((e->kind==VSJ_PROGRAM_DRAW&&e->result!=1)||(e->kind!=VSJ_PROGRAM_DRAW&&e->result!=0))return fail(r,VSJ_PAYLOAD);
  if(!post(r,flags))return fail(r,VSJ_RESOURCE);
  if(e->kind==VSJ_CLEAR){const VsjClearArguments*a=e->arguments;size_t bytes=sizeof(VsrClearNative)+(size_t)a->count*16;if(bytes>r->scratch_cap||(a->count&&!a->rectangles))return fail(r,VSJ_PAYLOAD);VsrClearNative n={VSR_VERSION,a->flags,a->color,a->stencil,a->count,r->target_active,a->depth,{0}};if(!r->c.view(r->c.user,&n.view))return fail(r,VSJ_RESOURCE);memcpy(r->scratch,&n,sizeof n);if(a->count)memcpy(r->scratch+sizeof n,a->rectangles,(size_t)a->count*16);VsjChunk c={VSJ_CHUNK_NATIVE_ARGS,r->scratch,bytes};return commit(r,&c,1);}
  memset(&r->draw,0,sizeof r->draw);r->draw.version=VSR_VERSION;r->draw.kind=e->kind;r->draw.target_active=r->target_active;if(!material(r))return fail(r,VSJ_PAYLOAD);
  if(e->kind==VSJ_PROGRAM_DRAW){if(e->argument_bytes!=sizeof(VsjProgramArguments)||!e->arguments)return fail(r,VSJ_PAYLOAD);const VsjProgramArguments*a=e->arguments;r->draw.used=a->used;r->draw.vertex_count=a->vertex_count;r->draw.index_count=a->index_count;r->draw.program_hash=a->hash;r->draw.constant_revision=a->constant_revision;r->draw.zscale=a->zscale;memcpy(r->draw.texscale,a->texscale,sizeof a->texscale);r->waiting=1;r->discard_source=0;return 1;}
  if(e->argument_bytes!=sizeof(VsjUpArguments)||!e->arguments||!r->geometry_ready)return fail(r,VSJ_PAYLOAD);const VsjUpArguments*a=e->arguments;
  r->draw.primitive=a->primitive;r->draw.primitive_count=a->primitives;r->draw.vertex_count=r->geometry.vertex_count;r->draw.vertex_stride=r->geometry.stride;
  r->draw.geometry_encoding=VSR_GEOMETRY_FIXED_GPU32;r->draw.emitted_vertex_count=r->geometry.vertex_count;r->draw.emitted_vertex_stride=r->geometry.stride;
  VsjChunk c[]={{VSJ_CHUNK_GEOMETRY,r->scratch,r->geometry.bytes},{VSJ_CHUNK_MATERIAL,r->material,r->material_bytes},{VSJ_CHUNK_NATIVE_ARGS,&r->draw,sizeof r->draw}};return commit(r,c,3);
 }
 if(e->phase==VSJ_POST&&(e->kind==VSJ_VIEW||e->kind==VSJ_TARGET_SELECT)){
  VsrStateNative n;memset(&n,0,sizeof n);n.version=VSR_VERSION;n.result=(uint32_t)e->result;n.target_active=r->target_active;
  if(r->ticket||!r->c.view(r->c.user,&n.view))return fail(r,VSJ_PENDING);
  if(e->kind==VSJ_TARGET_SELECT){n.opcode=VSR_STATE_TARGET;unsigned count=0;if(e->result!=1)return 1;if(!r->c.bindings(r->c.user,3,n.resources,6,&count)||!count||count>6)return fail(r,VSJ_RESOURCE);n.resource_count=count;}
  else {size_t bytes=0;if(e->function&&!strcmp(e->function,"d3d8_SetScissorRect")){n.opcode=VSR_STATE_SCISSOR;bytes=16;}
   else if(e->function&&!strcmp(e->function,"d3d8_vk_set_polygon_offset")){n.opcode=VSR_STATE_POLYGON;bytes=12;}
   else if(e->function&&!strcmp(e->function,"d3d8_SetBlendColor")){n.opcode=VSR_STATE_BLEND;bytes=16;}
   else if(e->function&&!strcmp(e->function,"d3d8_SetDrawPlacement")){n.opcode=VSR_STATE_PLACEMENT;bytes=8;}
   else if(e->function&&!strcmp(e->function,"d3d8_Nv2aPointMode")){n.opcode=VSR_STATE_POINT;bytes=8;}
   if(!bytes||e->argument_bytes!=bytes||!e->arguments)return fail(r,VSJ_PAYLOAD);n.scalar_bytes=(uint32_t)bytes;memcpy(n.scalar,e->arguments,bytes);
  }
  VsjChunk c={VSJ_CHUNK_NATIVE_ARGS,&n,sizeof n};return vsj_state(r->j,e->kind,&c,1)?1:fail(r,vsj_status(r->j));
 }
 if(e->kind==VSJ_FINAL_TARGET&&e->phase==VSJ_POST){VfpResource v;if(r->ticket||!r->completed_seen||e->argument_bytes!=4||!e->arguments||e->result!=1||*(const uint32_t*)e->arguments!=r->completed||!r->c.completed(r->c.user,r->completed,&v))return fail(r,VSJ_RESOURCE);struct{uint32_t version,reserved;uint64_t identity;}n={1,0,v.identity};VsjChunk c={VSJ_CHUNK_NATIVE_ARGS,&n,sizeof n};r->final=v.identity;return vsj_state(r->j,VSJ_FINAL_TARGET,&c,1)?1:fail(r,vsj_status(r->j));}
 return fail(r,VSJ_UNKNOWN_WRITER);
}
static int samefn(const char*a,const char*b){return a&&strcmp(a,b)==0;}
static int raw(void*u,uint32_t kind,uint32_t domain,const char*fn,unsigned line){
 VsrRecorder*r=u;(void)line;if(!own(r))return fail(r,VSJ_OWNER);
 if(!current_matches(r))return 0;
 if(domain==VSJ_DOMAIN_GLOBAL){r->stats.raw_global++;if((kind==VSJ_RAW_CLEAR_IMAGE&&samefn(fn,"device_make"))||(kind==VSJ_RAW_UPLOAD&&(samefn(fn,"upload_level")||samefn(fn,"cube_upload")))){r->c.invalidate(r->c.user,VSJ_GENERATION);return 1;}return fail(r,VSJ_UNKNOWN_WRITER);}
 if(domain==VSJ_DOMAIN_HOST_PRESENT){
  r->stats.raw_host++;if(!r->active||!r->host_entered)return fail(r,VSJ_GENERATION);
  if(r->source_only_seen)return fail(r,VSJ_UNKNOWN_WRITER);
  if(kind==VSJ_RAW_BLIT&&samefn(fn,"rt_present_copy")){if(++r->host.raw_copies!=1)return fail(r,VSJ_UNKNOWN_WRITER);return 1;}
  if(kind==VSJ_RAW_DRAW&&samefn(fn,"gamma_draw_vsj_body")){if(!r->gamma_pending||++r->gamma_raw!=1||++r->host.raw_gamma!=1)return fail(r,VSJ_UNKNOWN_WRITER);return 1;}
  if(kind==VSJ_RAW_CLEAR_IMAGE&&samefn(fn,"dev_Present_vsj_body")){if(++r->host.raw_swap_clears!=1)return fail(r,VSJ_UNKNOWN_WRITER);return 1;}
  if(kind==VSJ_RAW_BLIT&&samefn(fn,"dev_Present_vsj_body")){
   D3D8SceneHostPolicy p;memset(&p,0,sizeof p);
   if(++r->host.raw_swap_blits!=1||!r->c.host_policy(r->c.user,&p)||p.version!=1||p.bytes!=sizeof p)return fail(r,VSJ_PAYLOAD);
   if(r->policy_seen&&memcmp(&r->host.policy,&p,sizeof p))return fail(r,VSJ_GENERATION);
   if(r->host.raw_gamma&&(p.gamma_enabled!=r->gamma_enabled||memcmp(p.gamma,r->gamma,sizeof r->gamma)))return fail(r,VSJ_GENERATION);
   r->host.policy=p;r->policy_seen=1;return 1;
  }
  return fail(r,VSJ_UNKNOWN_WRITER);
 }
 if(domain!=VSJ_DOMAIN_SCENE)return fail(r,VSJ_UNKNOWN_WRITER);
 r->stats.raw_scene++;
 if(!r->active)return fail(r,VSJ_GENERATION);
 if(r->bad)return 1;
 int known=(r->kind==VSJ_PROGRAM_DRAW&&(kind==VSJ_RAW_DRAW||kind==VSJ_RAW_INDEXED_DRAW)&&samefn(fn,"d3d8_Nv2aProgramDraw_vsj_body"))||
 (r->kind==VSJ_UP_DRAW&&kind==VSJ_RAW_DRAW&&samefn(fn,"dev_DrawPrimitiveUP_vsj_body"))||
 (r->kind==VSJ_CLEAR&&kind==VSJ_RAW_CLEAR_ATTACHMENTS&&samefn(fn,"dev_Clear_vsj_body"))||
 (r->kind==VSJ_UPLOAD&&kind==VSJ_RAW_UPLOAD&&(samefn(fn,"upload_level")||samefn(fn,"cube_upload")));
 if(!r->ticket||r->waiting||!known||(r->kind==VSJ_UP_DRAW&&!r->geometry_ready))return fail(r,VSJ_UNKNOWN_WRITER);
 if(r->kind!=VSJ_CLEAR&&r->raw)return fail(r,VSJ_UNKNOWN_WRITER);
 r->raw++;vsj_writer(r->j,r->kind);return vsj_status(r->j)==VSJ_OK?1:fail(r,vsj_status(r->j));
}
static int boundary(void*u,uint32_t kind,uint64_t frame,uint32_t color){
 VsrRecorder*r=u;if(!own(r))return fail(r,VSJ_OWNER);
 if(kind!=VSJ_SCENE_ENTER&&!current_matches(r)&&kind!=VSJ_HOST_LEAVE)return 0;
 if(kind==VSJ_SCENE_ENTER){
  if(r->attempted&&frame==r->last_attempt)return current_matches(r);
  r->attempted=1;r->last_attempt=frame;
  if(r->active){fail(r,VSJ_PENDING);VsjFrame*f=NULL;vsj_seal(r->j,0,&f);if(f)vsj_release(&f);r->active=0;}
  VsjStamp s;VsrSourceTag tag;VfpProvider p;uint64_t epoch=0;memset(&s,0,sizeof s);memset(&p,0,sizeof p);memset(&tag,0,sizeof tag);
  if(!r->c.current(r->c.user,frame,&s)||s.frame_serial!=frame||!s.continuity.scene||!s.continuity.view||!r->c.source_tag(r->c.user,&tag)||tag.version!=1||tag.reserved||tag.scene!=s.continuity.scene||tag.view!=s.continuity.view||tag.simulation_tick!=s.simulation_tick||!tag.authority_revision||!tag.boundary_ns||!r->c.provider(r->c.user,&p,&epoch)||!epoch)return fail(r,VSJ_GENERATION);
  s.device_epoch=epoch;
  if(r->j&&(p.user!=r->provider.user||p.generation!=r->provider.generation||p.retain!=r->provider.retain||p.validate!=r->provider.validate||p.release!=r->provider.release))return fail(r,VSJ_GENERATION);
  if(!r->j){r->provider=p;VsjConfig c={r->c.user,r->c.owner,p,r->c.max_bytes-sizeof*r-r->scratch_cap,VSJ_MAX_COMMANDS,3};r->j=vsj_create(&c);if(!r->j)return fail(r,VSJ_BUDGET);}
  if(!vsj_begin(r->j,&s))return fail(r,VSJ_GENERATION);
  r->stamp=s;r->source=tag;r->active=1;r->bad=0;r->waiting=0;r->ticket=0;r->raw=0;r->kind=0;r->final=0;r->completed=0;r->completed_seen=0;r->host_entered=0;r->upload_ready=0;r->discard_source=0;r->gamma_pending=0;r->policy_seen=0;r->source_only_seen=0;memset(&r->source_only,0,sizeof r->source_only);memset(&r->host,0,sizeof r->host);r->host.version=VSR_VERSION;r->host.source=tag;r->stats.begun++;return 1;
 }
 if(kind==VSJ_SCENE_COMPLETED_TARGET){if(!r->active||r->waiting||r->ticket||r->upload_ready)return fail(r,VSJ_PENDING);r->completed=color;r->completed_seen=1;return 1;}
 if(kind==VSJ_HOST_ENTER){if(!r->active||!r->final||!r->completed_seen||r->host_entered||r->ticket||r->waiting)return fail(r,VSJ_PENDING);r->host_entered=1;return 1;}
 if(kind==VSJ_HOST_LEAVE){
  if(!r->active)return fail(r,VSJ_GENERATION);
  if(r->source_only_seen){
   if(!r->source_only_publish||r->host.raw_swap_clears||r->host.raw_swap_blits||r->host.raw_gamma||r->gamma_pending||r->host.gamma_applied||r->policy_seen)fail(r,VSJ_MISSING_WRITER);
  }else if(!r->policy_seen||r->host.raw_swap_clears!=1||r->host.raw_swap_blits!=1||r->gamma_pending||r->host.raw_gamma!=r->host.gamma_applied)fail(r,VSJ_MISSING_WRITER);
  VsjFrame*f=NULL;int ok=vsj_seal(r->j,r->final,&f);r->active=0;r->ticket=0;r->waiting=0;
  if(!ok)return fail(r,vsj_status(r->j));
  int accepted;
  if(r->source_only_seen){VsrSourceOnlyPolicy p={1,0,r->source,r->source_only};accepted=r->source_only_publish(r->source_only_user,&f,&p);}
  else accepted=r->c.publish(r->c.user,&f,&r->host);
  if(!accepted||f){if(f)vsj_release(&f);return fail(r,VSJ_INVALID);}r->stats.published++;return 1;
 }
 return fail(r,VSJ_INVALID);
}
VsrRecorder*vsr_create(const VsrConfig*c){
 if(!c||!c->owner||!c->current||!c->source_tag||!c->provider||!c->prepare||!c->bindings||!c->upload||!c->material||!c->view||!c->completed||!c->host_policy||!c->publish||!c->invalidate||c->max_bytes>VSJ_MAX_BYTES||!c->upload_budget||c->upload_budget>VSR_MAX_UPLOAD||c->max_bytes<=sizeof(VsrRecorder)+c->upload_budget+1024u*1024u)return NULL;
 VsrRecorder*r=calloc(1,sizeof*r);if(!r)return NULL;r->c=*c;r->scratch_cap=c->upload_budget;r->scratch=malloc(r->scratch_cap);if(!r->scratch){free(r);return NULL;}
 VsjBackendHooks h={r,event,raw,boundary,fault};if(!vsj_backend_install(&h)){free(r->scratch);free(r);return NULL;}return r;
}
int vsr_stats(const VsrRecorder*r,VsrStats*out){if(!r||!out||!r->c.owner(r->c.user))return 0;*out=r->stats;return 1;}

int vsr_program_lineage(VsrRecorder*r,const VsrPacketLineage*p,const VfpSource*s){
 if(!r||!own(r))return 0;
 if(!current_matches(r)||r->bad)return 0;
 if(r->discard_source&&!r->waiting)return 1; /* no actual program writer */
 if(!r->active||!r->waiting||!r->ticket||r->kind!=VSJ_PROGRAM_DRAW||!p||p->version!=1||!p->allocation_identity||!p->allocation_epoch||p->end<=p->begin||(p->begin&3)||(p->end&3)||p->method!=0x17fcu||p->subchannel>7||memcmp(&p->source,&r->source,sizeof r->source)||r->draw.lineage.version||!s||s->version!=1||s->original_frame!=r->stamp.frame_serial||s->used!=r->draw.used||s->program_hash!=r->draw.program_hash||s->nvertices!=r->draw.vertex_count||s->nindices!=r->draw.index_count||s->constant_revision!=r->draw.constant_revision)return fail(r,VSJ_PAYLOAD);
 r->draw.lineage=*p;return 1;
}

int vsr_source_only_configure(VsrRecorder*r,VsrSourceOnlyPublish publish,void*user){
 if(!r||!publish||r->attempted||r->active||r->source_only_publish)return 0;
 r->source_only_publish=publish;r->source_only_user=user;return 1;
}

int vsr_program_lineage_status(VsrRecorder*r,const VsrPacketLineage*p,const VfpSource*s){
 if(!r||!own(r))return VSR_LINEAGE_REJECT;
 if(r->attempted&&(!r->active||r->bad)){
  VsrSourceTag tag;memset(&tag,0,sizeof tag);
  unsigned per=0;if(s)for(unsigned i=0;i<16;i++)per+=(s->used>>i)&1u;if(!per)per=1;
  if(!p||p->version!=1||!p->allocation_identity||!p->allocation_epoch||p->end<=p->begin||(p->begin&3)||(p->end&3)||p->method!=0x17fc||p->subchannel>7||
   !r->c.source_tag(r->c.user,&tag)||tag.version!=1||tag.reserved||!tag.scene||!tag.view||!tag.authority_revision||!tag.boundary_ns||memcmp(&p->source,&tag,sizeof tag)||
   !s||s->version!=VFP_VERSION||s->original_frame!=r->last_attempt||!s->nvertices||!s->nindices||!s->program_revision||!s->constant_revision||s->program_start>=136||
   (s->primitive!=1&&s->primitive!=5)||s->camera.encoding!=VFP_CAMERA_BAKED_NV2A||(s->used&0xffff0000u)||!s->vertices||!s->indices||!s->program||!s->constants||
   s->program_bytes!=136u*16||s->constant_bytes!=192u*16||s->vertex_bytes!=(size_t)s->nvertices*per*16||s->index_bytes!=(size_t)s->nindices*2)return VSR_LINEAGE_REJECT;
  /* The already rejected frame remains rejected. No journal mutation,
   * validation bypass, publication or new authority is created here. */
  return VSR_LINEAGE_DISCARDED;
 }
 return vsr_program_lineage(r,p,s)?VSR_LINEAGE_ACCEPT:VSR_LINEAGE_REJECT;
}
