/* Optional CPU/GPU-owned proof; no original update invocation. djultra64. */
#include "actor_draw_binding.h"
#include "menu_pc_sha256.h"
#include <string.h>
#include <math.h>
#include <limits.h>
static int equal_tag(const VpliSceneTag*a,const VpliSceneTag*b){return !memcmp(a,b,sizeof*a);}
static int valid_tag(const VpliSceneTag*t){return t&&t->version==1&&!t->reserved&&t->scene&&t->view&&t->authority_revision&&t->boundary_ns;}
static int spans(const VabSpan*a,const VabSpan*b){return a->identity&&a->epoch&&a->identity==b->identity&&a->epoch==b->epoch&&a->wrap_generation==b->wrap_generation&&a->begin<=b->begin&&b->begin<b->end&&b->end<=a->end&&!((a->begin|a->end|b->begin|b->end)&3);}
static int fail(VadbCollector*c,unsigned why){if(c)c->failed=why;return why;}
static int active(VadbCollector*c){return c&&c->initialized&&c->config.enabled;}
static int cpu(VadbCollector*c){return active(c)&&c->config.cpu_owner(c->config.user);}
static int read32(VadbCollector*c,uint32_t at,uint32_t*out){return at<=UINT32_MAX-4&&c->config.reader.read(c->config.reader.user,at,out,4);}
static int position(VadbCollector*c,VabSpan*b){uint32_t put;return read32(c,0x1b9440,&put)&&c->config.cursor(c->config.user,put,b);}
static int end_position(VadbCollector*c,const VabSpan*start,VabSpan*out){VabSpan now;if(!position(c,&now)||now.identity!=start->identity||now.epoch!=start->epoch||now.wrap_generation!=start->wrap_generation||now.begin<start->begin)return 0;*out=*start;out->end=now.begin;return 1;}
static int unchanged(VadbCollector*c){VpliSceneTag t;return cpu(c)&&!c->failed&&c->config.staged_source(c->config.user,&t)&&equal_tag(&t,&c->source)&&c->config.owner_ctor(c->config.user,c->owner,&t);}
static VadbMeshLife*life(VadbCollector*c,uint32_t va){for(unsigned i=0;i<VADB_MESH_LIVES;i++)if(c->lives[i].va==va)return c->lives+i;return NULL;}
static const VabChunk*chunk(const VabRecord*r,unsigned role,unsigned nth){for(unsigned i=0;i<r->chunk_count;i++)if(r->chunks[i].role==role&&!nth--)return r->chunks+i;return NULL;}
static const unsigned char*bytes(const VabRecord*r,const VabChunk*b){return b&&b->offset<=r->used&&b->bytes<=r->used-b->offset?r->data+b->offset:NULL;}
static uint32_t u32(const unsigned char*p){uint32_t n;memcpy(&n,p,4);return n;}
int vadb_configure(VadbCollector*c,const VadbConfig*p){if(!c||!p||c->initialized||p->enabled<0||p->enabled>1)return VADB_INVALID;if(!p->enabled){c->initialized=1;return VADB_OFF;}if(!p->reader.enabled||!p->reader.owner||!p->reader.epoch||!p->reader.read||!p->cpu_owner||!p->gpu_owner||!p->staged_source||!p->current_source||!p->owner_ctor||!p->cursor)return VADB_INVALID;memset(c,0,sizeof*c);c->config=*p;if(pthread_mutex_init(&c->mutex,NULL))return VADB_INVALID;c->initialized=1;return VADB_OK;}
uint64_t vadb_mesh_ctor_begin(VadbCollector*c,uint32_t va){if(!cpu(c)||!va)return 0;pthread_mutex_lock(&c->mutex);VadbMeshLife*l=life(c,va);if(!l)for(unsigned i=0;i<VADB_MESH_LIVES;i++)if(!c->lives[i].va||!c->lives[i].live){l=c->lives+i;break;}uint64_t id=0;if(l&&c->next_mesh!=UINT64_MAX){id=++c->next_mesh;*l=(VadbMeshLife){va,0,id};}pthread_mutex_unlock(&c->mutex);if(!id)fail(c,VADB_BUDGET);return id;}
int vadb_mesh_ctor_end(VadbCollector*c,uint32_t va,uint64_t ticket,uint32_t result){if(!cpu(c)||!ticket||va!=result)return VADB_MISSING;uint32_t vt;if(!read32(c,va,&vt)||vt!=0x22c1ac)return VADB_MISSING;pthread_mutex_lock(&c->mutex);VadbMeshLife*l=life(c,va);int ok=l&&l->id==ticket&&!l->live;if(ok)l->live=1;pthread_mutex_unlock(&c->mutex);return ok?VADB_OK:VADB_MISSING;}
void vadb_mesh_dtor(VadbCollector*c,uint32_t va){if(!cpu(c))return;pthread_mutex_lock(&c->mutex);VadbMeshLife*l=life(c,va);if(l)l->live=0;pthread_mutex_unlock(&c->mutex);}
int vadb_owner_begin(VadbCollector*c,uint32_t owner){if(!cpu(c)||c->scope)return VADB_MISSING;VpliSceneTag t;if(!c->config.staged_source(c->config.user,&t)||!valid_tag(&t)||!c->config.owner_ctor(c->config.user,owner,&t))return VADB_MISSING;c->owner=owner;c->source=t;c->scope=1;c->failed=0;c->owner_slots=0;return VADB_OK;}
int vadb_owner_count(VadbCollector*c,uint32_t caller,uint32_t n){if(!unchanged(c)||c->scope!=1||caller!=0xf1a6c||!n||n>2)return fail(c,VADB_MISSING);c->owner_slots=n*2;return VADB_OK;}
int vadb_owner_end(VadbCollector*c){if(!cpu(c))return VADB_MISSING;int ok=c->scope==1&&!c->failed;pthread_mutex_lock(&c->mutex);for(unsigned i=0;i<VADB_RECORDS;i++)if(c->records[i].completed==1||(!ok&&c->records[i].completed==2&&c->records[i].owner==c->owner&&equal_tag(&c->records[i].source,&c->source))){c->records[i].completed=0;c->used--;}pthread_mutex_unlock(&c->mutex);c->scope=0;return ok?VADB_OK:fail(c,VADB_MISSING);}

int vadb_actor_begin(VadbCollector*c,uint32_t actor,uint32_t caller){if(!unchanged(c)||c->scope!=1||!c->owner_slots||actor<c->owner+0x34||(actor-c->owner-0x34)/0x58>=c->owner_slots)return fail(c,VADB_MISSING);VabSpan s;if(!position(c,&s))return fail(c,VADB_MISSING);VabStamp stamp={c->source.scene,c->source.view,0,c->source.simulation_tick,0};int z=vab_actor_begin_current_owner(&c->config.reader,c->owner,actor,caller,&stamp,&s,&c->scratch);if(z!=VAB_UNBOUND)return fail(c,VADB_MISSING);c->actor=actor;c->actor_chunks=c->scratch.chunk_count;c->actor_bytes=c->scratch.used;c->scope=2;return VADB_OK;}
int vadb_model_begin(VadbCollector*c,uint32_t model){if(!unchanged(c)||c->scope!=2)return fail(c,VADB_MISSING);VabSpan s;if(!position(c,&s)||vab_model_begin(&c->config.reader,model,&s,&c->scratch)!=VAB_UNBOUND)return fail(c,VADB_MISSING);const VabChunk*l=chunk(&c->scratch,VAB_LOD_LIST,0);const unsigned char*p=bytes(&c->scratch,l);if(!p)return fail(c,VADB_INVALID);unsigned found=0;for(unsigned i=0;i<l->bytes/4;i++)if(u32(p+4*i)==model){c->lod_index=i;found++;}if(found!=1)return fail(c,VADB_AMBIGUOUS);c->model=model;c->base_chunks=c->scratch.chunk_count;c->base_bytes=c->scratch.used;c->scope=3;return VADB_OK;}
int vadb_mesh_begin(VadbCollector*c,uint32_t mesh){if(!unchanged(c)||c->scope!=3)return fail(c,VADB_MISSING);const VabChunk*l=chunk(&c->scratch,VAB_MESH_LIST,0);const unsigned char*p=bytes(&c->scratch,l);unsigned found=0;if(!p)return fail(c,VADB_INVALID);for(unsigned i=0;i<l->bytes/12;i++)found+=u32(p+12*i)==mesh;if(found!=1)return fail(c,VADB_AMBIGUOUS);if(!c->config.reader.read(c->config.reader.user,mesh,c->mesh_header,sizeof c->mesh_header)||u32(c->mesh_header)!=0x22c1ac||!position(c,&c->mesh_span))return fail(c,VADB_MISSING);pthread_mutex_lock(&c->mutex);VadbMeshLife*life_record=life(c,mesh);c->mesh_lifetime=life_record&&life_record->live?life_record->id:0;pthread_mutex_unlock(&c->mutex);if(!c->mesh_lifetime)return fail(c,VADB_MISSING);c->mesh=mesh;c->scope=4;return VADB_OK;}
int vadb_upload_begin(VadbCollector*c,uint32_t caller,uint32_t renderer,uint32_t palette,uint32_t indices,uint32_t count){if(!unchanged(c)||c->scope!=4||palette!=u32(c->mesh_header+0x1a0)||indices!=u32(c->mesh_header+0x1a8)||count!=u32(c->mesh_header+0x1a4))return fail(c,VADB_MISSING);VabSpan s;if(!position(c,&s)||vab_upload_begin(&c->config.reader,caller,renderer,palette,indices,count,&s,&c->scratch)!=VAB_UNBOUND)return fail(c,VADB_MISSING);c->scope=5;return VADB_OK;}
int vadb_upload_end(VadbCollector*c){if(!unchanged(c)||c->scope!=5)return fail(c,VADB_MISSING);VabSpan s;if(!end_position(c,&c->scratch.upload_span,&s)||vab_upload_end(&c->config.reader,&s,&c->scratch)!=VAB_UNBOUND)return fail(c,VADB_MISSING);c->scope=4;return VADB_OK;}
int vadb_mesh_end(VadbCollector*c){if(!unchanged(c)||c->scope!=4||!c->scratch.upload_count)return fail(c,VADB_MISSING);VabSpan s;if(!end_position(c,&c->mesh_span,&s)||s.begin!=c->mesh_span.begin||!s.end||s.end<=s.begin)return fail(c,VADB_MISSING);c->mesh_span=s;if(!spans(&s,&c->scratch.upload_span))return fail(c,VADB_MISSING);pthread_mutex_lock(&c->mutex);VadbCpuRecord*r=NULL;for(unsigned i=0;i<VADB_RECORDS;i++)if(!c->records[i].completed){r=c->records+i;break;}if(r){memset(r,0,sizeof*r);r->graph=c->scratch;r->source=c->source;r->mesh_span=s;r->owner=c->owner;r->actor_index=(c->actor-c->owner-0x34)/0x58;r->owner_slots=c->owner_slots;r->mesh=c->mesh;r->lod_index=c->lod_index;r->mesh_lifetime=c->mesh_lifetime;memcpy(r->mesh_header,c->mesh_header,sizeof r->mesh_header);r->completed=1;c->used++;}pthread_mutex_unlock(&c->mutex);if(!r)return fail(c,VADB_BUDGET);c->scratch.chunk_count=c->base_chunks;c->scratch.used=c->base_bytes;c->scratch.upload_count=0;memset(&c->scratch.upload_span,0,sizeof c->scratch.upload_span);c->mesh=0;c->scope=3;return VADB_OK;}
int vadb_model_end(VadbCollector*c){if(!unchanged(c)||c->scope!=3)return fail(c,VADB_MISSING);VabSpan s;if(!end_position(c,&c->scratch.model_span,&s)||vab_model_end(&c->config.reader,&s,&c->scratch)!=VAB_UNBOUND)return fail(c,VADB_MISSING);pthread_mutex_lock(&c->mutex);for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*r=c->records+i;if(r->completed==1&&r->graph.actor==c->actor&&r->graph.model==c->model&&equal_tag(&r->source,&c->source))r->graph.model_span=s;}pthread_mutex_unlock(&c->mutex);c->model=0;c->scratch.model=0;c->scratch.chunk_count=c->actor_chunks;c->scratch.used=c->actor_bytes;c->scope=2;return VADB_OK;}
int vadb_actor_end(VadbCollector*c){
 if(!unchanged(c)||c->scope!=2)return fail(c,VADB_MISSING);
 VabSpan s;if(!end_position(c,&c->scratch.actor_span,&s))return fail(c,VADB_MISSING);
 int ok=1;unsigned matched=0;pthread_mutex_lock(&c->mutex);
 for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*r=c->records+i;
  if(r->completed==1&&r->graph.actor==c->actor&&equal_tag(&r->source,&c->source)){
   matched++;int z=vab_actor_end(&c->config.reader,&s,&r->graph);
   if(z!=VAB_UNBOUND||!spans(&r->graph.model_span,&r->mesh_span))ok=0;
  }
 }
 if(!matched)ok=0;
 /* Atomic publication: a late invalid mesh cannot expose an earlier partial
  * actor. Readers share this lock and see all closed records or none. */
 for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*r=c->records+i;
  if(r->completed==1&&r->graph.actor==c->actor&&equal_tag(&r->source,&c->source)){
   r->completed=ok?2:0;if(!ok)c->used--;
  }
 }
 pthread_mutex_unlock(&c->mutex);c->actor=0;c->scope=1;
 return ok?VADB_OK:fail(c,VADB_MISSING);
}
int vadb_retire_before(VadbCollector*c,const VplSpan*next){if(!active(c)||!c->config.gpu_owner(c->config.user)||!next||!next->identity||!next->epoch)return VADB_MISSING;pthread_mutex_lock(&c->mutex);for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*r=c->records+i;if(r->completed==2&&r->graph.actor_span.identity==next->identity&&r->graph.actor_span.epoch==next->epoch&&r->graph.actor_span.wrap_generation==next->wrap_generation&&r->graph.actor_span.end<=next->begin){r->completed=0;c->used--;}}pthread_mutex_unlock(&c->mutex);return VADB_OK;}
/* Rig key is deliberately conservative. Full unknown model/mesh/attachment
 * changes reject; pose palette bytes alone are allowed to differ. */
static int rig_key(const VadbCpuRecord*r,char out[65]){
 unsigned char keys[VAB_CHUNKS*(65+12)+0x1b4];size_t n=0;
 memcpy(keys,r->mesh_header,sizeof r->mesh_header);n=sizeof r->mesh_header;
 for(unsigned i=0;i<r->graph.chunk_count;i++){
  const VabChunk*c=r->graph.chunks+i;const unsigned char*p=bytes(&r->graph,c);if(!p)return 0;
  if(c->role==VAB_UPLOAD_MATRIX||c->role==VAB_ATTACHMENT_MATRIX)continue;
  if(n+77>sizeof keys)return 0;memcpy(keys+n,&c->role,4);memcpy(keys+n+4,&c->address,4);memcpy(keys+n+8,&c->bytes,4);n+=12;
  /* These structures hold evolving transform/animation state, but their
   * captured addresses and shape remain part of the key. Exact final rows
   * are proved independently against every bound physical register. */
  if(c->role==VAB_TRANSFORM||c->role==VAB_COMPONENT||c->role==VAB_LOCAL_PALETTE||c->role==VAB_WORLD_PALETTE||c->role==VAB_DESCRIPTOR||c->role==VAB_MANAGER){memset(keys+n,0,65);}
  else menu_pc_sha256(p,c->bytes,(char*)keys+n);
  n+=65;
 }
 menu_pc_sha256(keys,n,out);return 1;
}
static int finite_float(const float*p){uint32_t x;memcpy(&x,p,4);return (x&0x7f800000u)!=0x7f800000u;}
static int source_shape(const VfpSource*s){
 if(!s||s->version!=1||!s->program_revision||!s->constant_revision||!s->program||!s->vertices||!s->indices||!s->constants||!s->nvertices||!s->nindices||s->program_bytes!=136u*16||s->constant_bytes!=VADB_BANK_BYTES||s->index_bytes!=(size_t)s->nindices*2||s->used&0xffff0000u)return 0;
 unsigned per=0;for(unsigned i=0;i<16;i++)per+=(s->used>>i)&1u;if(!per)per=1;
 return s->vertex_bytes==(size_t)s->nvertices*per*16&&s->vertex_bytes<=VFP_MAX_PACKET_BYTES&&s->index_bytes<=VFP_MAX_PACKET_BYTES;
}
static int resources_valid(const VfpResource*p,unsigned count){if(!p||!count||count>6)return 0;unsigned seen=0;for(unsigned i=0;i<count;i++){if(!p[i].identity||!p[i].version||p[i].role>5||!p[i].width||!p[i].height||!p[i].pitch||(seen&(1u<<p[i].role)))return 0;seen|=1u<<p[i].role;}return 1;}
static int resource_equal(const VfpResource*a,const VfpResource*b,int target_pair){return a->identity==b->identity&&(target_pair&&a->role<2?1:a->version==b->version)&&a->role==b->role&&a->format==b->format&&a->width==b->width&&a->height==b->height&&a->pitch==b->pitch;}
static int binding_valid(const VadbBinding*b){return b&&b->version==1&&b->map_count&&b->map_count<=VADB_MAX_MAPS&&b->resource_count<=6&&menu_pc_sha_is(b,offsetof(VadbBinding,seal),b->seal);}
int vadb_source_matches(const VadbBinding*b,const VfpSource*s,const VfpResource*r,unsigned count,const void*mat,size_t mat_bytes){
 if(!binding_valid(b)||!source_shape(s)||!resources_valid(r,count)||!mat||!mat_bytes||s->original_frame!=b->stamp.frame_serial||s->program_hash!=b->program_hash||s->program_revision!=b->program_revision||s->constant_revision!=b->constant_revision||s->program_start!=b->program_start||s->primitive!=b->primitive||s->used!=b->used||s->nvertices!=b->nvertices||s->nindices!=b->nindices||count!=b->resource_count||memcmp(s->constants,b->original_constants,VADB_BANK_BYTES))return VADB_MISSING;
 for(unsigned i=0;i<count;i++)if(!resource_equal(r+i,b->resources+i,0))return VADB_MISSING;
 return menu_pc_sha_is(s->program,s->program_bytes,b->program_sha)&&menu_pc_sha_is(s->vertices,s->vertex_bytes,b->geometry_sha)&&menu_pc_sha_is(s->indices,s->index_bytes,b->indices_sha)&&menu_pc_sha_is(mat,mat_bytes,b->material_sha)?VADB_OK:VADB_MISSING;
}
int vadb_bind_draw(VadbCollector*c,const VpliPacket*p,const VfpSource*s,uint64_t device,uint64_t serial,const VfpResource*resources,unsigned resource_count,const void*mat,size_t mat_bytes,const VclSnapshot*writers,VadbBinding*out){
 if(out)memset(out,0,sizeof*out);
 if(!active(c)||!c->config.gpu_owner(c->config.user))return VADB_OFF;
 if(!out||!p||p->version!=2||p->method!=0x17fc||!valid_tag(&p->scene)||!source_shape(s)||!device||!serial||!resources_valid(resources,resource_count)||!mat||!mat_bytes||mat_bytes>8192||!vcl_snapshot_bytes(writers)||writers->revision!=s->constant_revision)return VADB_MISSING;
 VpliSceneTag current;if(!c->config.current_source(c->config.user,&current)||!equal_tag(&current,&p->scene))return VADB_CHANGED;
 VabSpan draw={p->packet.identity,p->packet.epoch,p->packet.wrap_generation,p->packet.begin,p->packet.end};
 int result=VADB_MISSING;pthread_mutex_lock(&c->mutex);VadbCpuRecord*r=NULL;
 for(unsigned i=0;i<VADB_RECORDS;i++){VadbCpuRecord*t=c->records+i;if(t->completed==2&&equal_tag(&t->source,&p->scene)&&spans(&t->mesh_span,&draw)){if(r){result=VADB_AMBIGUOUS;goto done;}r=t;}}
 if(!r||!spans(&r->graph.actor_span,&draw)||!spans(&r->graph.model_span,&draw)||!spans(&r->mesh_span,&r->graph.upload_span)||r->graph.upload_span.end>draw.begin)goto done;
 VadbMeshLife*l=life(c,r->mesh);if(!l||!l->live||l->id!=r->mesh_lifetime)goto done;
 VabStamp actual={p->scene.scene,p->scene.view,s->original_frame,p->scene.simulation_tick,device};
 if(r->graph.status!=VAB_UNBOUND&&r->graph.status!=VAB_OK)goto done;
 if(r->graph.status==VAB_OK&&memcmp(&r->graph.stamp,&actual,sizeof actual))goto done;
 if(r->graph.status==VAB_UNBOUND&&(r->graph.stamp.scene!=actual.scene||r->graph.stamp.view!=actual.view||r->graph.stamp.simulation_tick!=actual.simulation_tick||r->graph.stamp.frame_serial||r->graph.stamp.device_epoch))goto done;
 if(!r->graph.upload_count||r->graph.upload_count>VADB_MAX_MAPS)goto done;
 memset(out,0,sizeof*out);out->version=1;out->owner=r->owner;out->actor_index=r->actor_index;out->owner_slots=r->owner_slots;out->actor=r->graph.actor;out->model=r->graph.model;out->mesh=r->mesh;out->lod_index=r->lod_index;out->mesh_lifetime=r->mesh_lifetime;out->memory_epoch=r->graph.memory_epoch;out->command_serial=serial;out->stamp=actual;out->source=p->scene;out->actor_span=r->graph.actor_span;out->model_span=r->graph.model_span;out->mesh_span=r->mesh_span;out->draw_span=draw;out->upload_span=r->graph.upload_span;
 out->program_hash=s->program_hash;out->program_revision=s->program_revision;out->constant_revision=s->constant_revision;out->primitive=s->primitive;out->used=s->used;out->nvertices=s->nvertices;out->nindices=s->nindices;out->program_start=s->program_start;
 if(!rig_key(r,out->rig_sha))goto bad_out;
 menu_pc_sha256(s->program,s->program_bytes,out->program_sha);menu_pc_sha256(s->vertices,s->vertex_bytes,out->geometry_sha);menu_pc_sha256(s->indices,s->index_bytes,out->indices_sha);menu_pc_sha256(mat,mat_bytes,out->material_sha);
 out->resource_count=resource_count;memcpy(out->resources,resources,resource_count*sizeof*resources);memcpy(out->original_constants,s->constants,VADB_BANK_BYTES);
 const VabChunk*ic=chunk(&r->graph,VAB_UPLOAD_INDICES,0),*sc=chunk(&r->graph,VAB_UPLOAD_SLOTS,0);const unsigned char*ip=bytes(&r->graph,ic),*sp=bytes(&r->graph,sc);
 if(!ip||!sp||ic->bytes!=r->graph.upload_count*2||sc->bytes!=ic->bytes)goto bad_out;
 unsigned occupied[192]={0};
 for(unsigned i=0;i<r->graph.upload_count;i++){
  VadbMap*m=out->maps+i;uint16_t index;int16_t slot;memcpy(&index,ip+i*2,2);memcpy(&slot,sp+i*2,2);int physical=slot+96;
  const VabChunk*mc=chunk(&r->graph,VAB_UPLOAD_MATRIX,i);const unsigned char*mp=bytes(&r->graph,mc);
  if(!mp||mc->bytes!=64||index>=256||physical<0||physical>189)goto bad_out;
  for(unsigned k=0;k<3;k++)if(occupied[physical+k]++)goto bad_out;
  VplSpan upload={r->graph.upload_span.identity,r->graph.upload_span.epoch,r->graph.upload_span.wrap_generation,r->graph.upload_span.begin,r->graph.upload_span.end};
  if(!vcl_snapshot_written_by(writers,(uint32_t)physical,3,&upload,&p->scene)||memcmp(mp,s->constants+physical*4,48))goto bad_out;
  for(unsigned k=0;k<12;k++){float v;memcpy(&v,mp+k*4,4);if(!finite_float(&v))goto bad_out;}
  m->palette_index=index;m->physical_register=(uint16_t)physical;m->logical_slot=slot;memcpy(m->rows,mp,48);out->map_count++;
 }
 if(r->graph.status==VAB_UNBOUND&&vab_bind_owned(&r->graph,&actual)!=VAB_OK)goto bad_out;
 if(vab_match_draw(&r->graph,&actual,&draw,serial,s->program_revision,s->constant_revision)!=VAB_OK)goto bad_out;
 menu_pc_sha256(writers,vcl_snapshot_bytes(writers),out->writer_snapshot_sha);
 menu_pc_sha256(out,offsetof(VadbBinding,seal),out->seal);result=vadb_source_matches(out,s,resources,resource_count,mat,mat_bytes);if(result!=VADB_OK)goto bad_out;goto done;
 bad_out:memset(out,0,sizeof*out);result=VADB_MISSING;
 done:pthread_mutex_unlock(&c->mutex);return result;
}
static int resources_pair(const VadbBinding*a,const VadbBinding*b){if(a->resource_count!=b->resource_count)return 0;for(unsigned i=0;i<a->resource_count;i++)if(!resource_equal(a->resources+i,b->resources+i,1))return 0;return 1;}

int vadb_pair(const VadbBinding*a,const VadbBinding*b){
 if(!binding_valid(a)||!binding_valid(b)||a->owner!=b->owner||a->actor_index!=b->actor_index||a->owner_slots!=b->owner_slots||a->actor!=b->actor||a->model!=b->model||a->mesh!=b->mesh||a->mesh_lifetime!=b->mesh_lifetime||a->lod_index!=b->lod_index||a->memory_epoch!=b->memory_epoch||a->stamp.scene!=b->stamp.scene||a->stamp.view!=b->stamp.view||a->stamp.device_epoch!=b->stamp.device_epoch||a->source.authority_revision!=b->source.authority_revision||b->source.simulation_tick-a->source.simulation_tick!=1u||b->source.boundary_ns<=a->source.boundary_ns||b->stamp.frame_serial!=a->stamp.frame_serial+1||b->constant_revision<=a->constant_revision||a->program_revision!=b->program_revision||a->program_hash!=b->program_hash||a->primitive!=b->primitive||a->used!=b->used||a->nvertices!=b->nvertices||a->nindices!=b->nindices||a->program_start!=b->program_start||memcmp(a->program_sha,b->program_sha,65)||memcmp(a->geometry_sha,b->geometry_sha,65)||memcmp(a->indices_sha,b->indices_sha,65)||memcmp(a->rig_sha,b->rig_sha,65)||memcmp(a->material_sha,b->material_sha,65)||a->draw_span.identity!=b->draw_span.identity||a->draw_span.epoch!=b->draw_span.epoch||a->map_count!=b->map_count||!resources_pair(a,b))return VADB_MISSING;
 unsigned mapped[192]={0};for(unsigned i=0;i<a->map_count;i++){const VadbMap*x=a->maps+i,*y=b->maps+i;if(x->palette_index!=y->palette_index||x->physical_register!=y->physical_register||x->logical_slot!=y->logical_slot)return VADB_MISSING;for(unsigned k=0;k<3;k++)mapped[x->physical_register+k]=1;}
 for(unsigned k=0;k<192;k++)if(!mapped[k]&&memcmp(a->original_constants[k],b->original_constants[k],16))return VADB_MISSING;
 return VADB_OK;
}
#include <fenv.h>
int vadb_interpolate(const VadbBinding*a,const VadbBinding*b,float alpha,float out[192][4]){
 if(!out||!finite_float(&alpha)||alpha<0||alpha>1||vadb_pair(a,b)!=VADB_OK)return VADB_MISSING;
 uintptr_t p=(uintptr_t)out,pa=(uintptr_t)a,pb=(uintptr_t)b;if(p>UINTPTR_MAX-VADB_BANK_BYTES||(p<pa+sizeof*a&&pa<p+VADB_BANK_BYTES)||(p<pb+sizeof*b&&pb<p+VADB_BANK_BYTES))return VADB_INVALID;
 if(alpha==0){memcpy(out,a->original_constants,VADB_BANK_BYTES);return VADB_OK;}if(alpha==1){memcpy(out,b->original_constants,VADB_BANK_BYTES);return VADB_OK;}
 fenv_t env;if(feholdexcept(&env))return VADB_INVALID;if(fesetround(FE_TONEAREST)){fesetenv(&env);return VADB_INVALID;}
 memcpy(out,a->original_constants,VADB_BANK_BYTES);for(unsigned i=0;i<a->map_count;i++){unsigned reg=a->maps[i].physical_register;for(unsigned k=0;k<12;k++){float x=a->maps[i].rows[k],y=b->maps[i].rows[k];out[reg+k/4][k%4]=(float)((double)x+((double)y-(double)x)*(double)alpha);}}
 fesetenv(&env);return VADB_OK;
}
