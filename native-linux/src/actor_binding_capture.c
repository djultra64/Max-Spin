/* CPU-only capture of bounded original actor graph. djultra64. */
#include "actor_binding_capture.h"
#include <string.h>
static int bad(VabRecord*r,unsigned status){r->status=status;
 return status;
 }
static int ready(const VabReader*p,VabRecord*r){uint64_t epoch;
 
 if(!p||!p->enabled)return VAB_OFF;
 
 if(!r||!p->owner||!p->epoch||!p->read)return VAB_INVALID;
 
 if(r->status!=VAB_UNBOUND&&r->status!=VAB_OK)return r->status;
 
 if(!p->owner(p->user)||!p->epoch(p->user,&epoch)||epoch!=r->memory_epoch)return bad(r,VAB_CHANGED);
 return VAB_OK;
 }
static int read32(const VabReader*p,uint32_t at,uint32_t*out){if(at>UINT32_MAX-4)return 0;
 return p->read(p->user,at,out,4);
 }
static int add(const VabReader*p,VabRecord*r,uint32_t role,uint32_t at,uint32_t n){if(!at||!n||at>UINT32_MAX-n)return bad(r,VAB_RANGE);
 
 if(r->chunk_count==VAB_CHUNKS||n>VAB_BYTES-r->used)return bad(r,VAB_BUDGET);
 
 if(!p->read(p->user,at,r->data+r->used,n))return bad(r,VAB_RANGE);
 r->chunks[r->chunk_count++]=(VabChunk){role,at,r->used,n};
 r->used+=n;
 return VAB_OK;
 }
static const VabChunk*find(const VabRecord*r,unsigned role){for(unsigned i=0;i<r->chunk_count;i++)if(r->chunks[i].role==role)return r->chunks+i;
 return 0;
 }
static uint32_t field(const VabRecord*r,unsigned role,unsigned off){uint32_t out=0;
 const VabChunk*c=find(r,role);
 
 if(c&&off<=c->bytes&&4<=c->bytes-off)memcpy(&out,r->data+c->offset+off,4);
 return out;
 }
static int same(const VabSpan*a,const VabSpan*b){return a->identity&&a->identity==b->identity&&a->epoch&&a->epoch==b->epoch&&a->wrap_generation==b->wrap_generation;
 }
static int stamped(const VabStamp*s){return s->scene&&s->view&&s->device_epoch;
 }
static int observed_class(const VabReader*p,VabRecord*r,uint32_t object,uint32_t table,uint32_t slot,uint32_t target){uint32_t vt=0,to=0;
 
 if(!read32(p,object,&vt))return bad(r,VAB_RANGE);
 
 if(vt!=table)return bad(r,VAB_CLASS);
 
 if(!read32(p,vt+slot,&to))return bad(r,VAB_RANGE);
 
 if(to!=target)return bad(r,VAB_CLASS);
 return add(p,r,VAB_VTABLE,vt,slot+4);
 }
#define TRY(x) do{int z=(x);if(z!=VAB_OK)return z;}while(0)
static int actor_begin(const VabReader*p,uint32_t owner,uint32_t actor,uint32_t caller,uint32_t replay,int authoritative,const VabStamp*stamp,const VabSpan*span,VabRecord*r){uint32_t descriptor=0,other=0,component,transform,attachment,palette,bones,lods;
 
 if(!p||!p->enabled)return VAB_OFF;
 
 if(!r||!p->owner||!p->epoch||!p->read||!stamp||!span)return VAB_INVALID;
 
 memset(r,0,sizeof *r);
 r->status=VAB_UNBOUND;
 r->actor=actor;
 r->stamp=*stamp;
 r->actor_span=*span;
 
 if(!p->owner(p->user)||!p->epoch(p->user,&r->memory_epoch)||!r->memory_epoch)return bad(r,VAB_CHANGED);
 
 if((caller!=0xf1bd8&&caller!=0xf1cae)||owner>UINT32_MAX-0x200||actor<owner+0x34||actor>owner+0x34+3*0x58||(actor-owner-0x34)%0x58)return bad(r,VAB_INVALID);
 
 TRY(observed_class(p,r,owner,0x236940,0x10,0xf1a30));
 TRY(observed_class(p,r,actor,0x245088,0x20,0x146320));
 
 if(!read32(p,owner+0x10,&descriptor)||(!authoritative&&!read32(p,replay+0x10,&other)))return bad(r,VAB_RANGE);
 
 if(!descriptor||(!authoritative&&descriptor!=other))return bad(r,VAB_INVALID);
 
 TRY(add(p,r,VAB_OWNER,owner,0x200));
 TRY(add(p,r,VAB_ACTOR,actor,0x58));
 TRY(add(p,r,VAB_DESCRIPTOR,descriptor,0x150));
 
 r->manager=field(r,VAB_ACTOR,0x1c);
 component=field(r,VAB_ACTOR,0x24);
 TRY(add(p,r,VAB_MANAGER,r->manager,0x11c));
 TRY(add(p,r,VAB_COMPONENT,component,0x3b0));
 
 transform=field(r,VAB_COMPONENT,0x358);
 attachment=field(r,VAB_COMPONENT,0x354);
 TRY(add(p,r,VAB_TRANSFORM,transform,0x98));
 TRY(add(p,r,VAB_ATTACHMENT,attachment,0x68));
 
 palette=field(r,VAB_ATTACHMENT,0x50);
 uint32_t index=field(r,VAB_COMPONENT,0x350);
 
 if(index==UINT32_MAX||index>VAB_BONES||palette>UINT32_MAX-(index+1)*64)return bad(r,VAB_RANGE);
 TRY(add(p,r,VAB_ATTACHMENT_MATRIX,palette+(index+1)*64,64));
 
 bones=field(r,VAB_MANAGER,0xc0);
 lods=field(r,VAB_MANAGER,0xa4);
 
 if(!bones||bones>VAB_BONES||!lods||lods>VAB_LODS)return bad(r,VAB_RANGE);
 
 TRY(add(p,r,VAB_LOCAL_PALETTE,field(r,VAB_MANAGER,0xc4),bones*64));
 TRY(add(p,r,VAB_WORLD_PALETTE,field(r,VAB_MANAGER,0xcc),bones*64));
 TRY(add(p,r,VAB_LOD_LIST,field(r,VAB_MANAGER,0xa0),lods*4));
 TRY(ready(p,r));
 r->status=VAB_UNBOUND;
 return VAB_UNBOUND;
 
}
int vab_model_begin(const VabReader*p,uint32_t model,const VabSpan*span,VabRecord*r){TRY(ready(p,r));
 
 if(r->status!=VAB_UNBOUND||!span||r->model)return bad(r,VAB_ORDER);
 const VabChunk*l=find(r,VAB_LOD_LIST);
 int found=0;
 
 if(!l)return bad(r,VAB_INVALID);
 for(unsigned i=0;i<l->bytes;i+=4){uint32_t item;
 memcpy(&item,r->data+l->offset+i,4);
 found|=item==model;
 }if(!found)return bad(r,VAB_INVALID);
 r->model=model;
 r->model_span=*span;
 TRY(add(p,r,VAB_MODEL,model,0x1b4));
 uint32_t count=field(r,VAB_MODEL,0xc8);
 
 if(!count||count>VAB_MESHES)return bad(r,VAB_RANGE);
 uint32_t list=field(r,VAB_MODEL,0xc4);
 TRY(add(p,r,VAB_MESH_LIST,list,count*12));
 
 for(unsigned i=0;i<count;i++){uint32_t node;
 
 if(!read32(p,list+i*12,&node))return bad(r,VAB_RANGE);
 TRY(add(p,r,VAB_MESH,node,0x1b4));
 uint32_t vt;
 
 if(!read32(p,node,&vt))return bad(r,VAB_RANGE);
 TRY(add(p,r,VAB_VTABLE,vt,0x1c));
 }
 TRY(ready(p,r));
 return VAB_UNBOUND;
 
}
int vab_upload_begin(const VabReader*p,uint32_t caller,uint32_t renderer,uint32_t palette,uint32_t indices,uint32_t count,const VabSpan*span,VabRecord*r){
 TRY(ready(p,r));
 if(!span||!r->model||r->upload_count||caller!=0x6e49b||!count||count>64||renderer>UINT32_MAX-0x5c)return bad(r,VAB_ORDER);
 uint32_t actual,slots;
 if(!read32(p,0x30c148,&actual)||actual!=renderer||!read32(p,renderer+0x58,&slots))return bad(r,VAB_RANGE);
 TRY(add(p,r,VAB_UPLOAD_INDICES,indices,count*2));
 TRY(add(p,r,VAB_UPLOAD_SLOTS,slots,count*2));
 const VabChunk*ic=find(r,VAB_UPLOAD_INDICES),*sc=find(r,VAB_UPLOAD_SLOTS);
 for(unsigned i=0;i<count;i++){
  uint16_t index;int16_t slot;
  memcpy(&index,r->data+ic->offset+i*2,2);memcpy(&slot,r->data+sc->offset+i*2,2);
  if(index>=VAB_BONES||slot < -96||slot>93||palette>UINT32_MAX-(uint32_t)index*64)return bad(r,VAB_RANGE);
  TRY(add(p,r,VAB_UPLOAD_MATRIX,palette+(uint32_t)index*64,64));
 }
 r->upload_count=count;r->upload_span=*span;TRY(ready(p,r));return VAB_UNBOUND;
}
int vab_upload_end(const VabReader*p,const VabSpan*end,VabRecord*r){TRY(ready(p,r));
 if(!end||!r->upload_count||!same(&r->upload_span,end)||end->begin!=r->upload_span.begin||end->end<=end->begin)return bad(r,VAB_ORDER);
 r->upload_span.end=end->end;return VAB_UNBOUND;
}
int vab_model_end(const VabReader*p,const VabSpan*end,VabRecord*r){TRY(ready(p,r));
 
 if(!end||!r->model||!same(&r->model_span,end)||end->end<r->model_span.begin||end->begin!=r->model_span.begin)return bad(r,VAB_ORDER);
 r->model_span.end=end->end;
 return VAB_UNBOUND;
 }
int vab_actor_end(const VabReader*p,const VabSpan*end,VabRecord*r){TRY(ready(p,r));
 
 if(r->status!=VAB_UNBOUND||!end||!same(&r->actor_span,end)||end->begin!=r->actor_span.begin||end->end<end->begin)return bad(r,VAB_ORDER);
 r->actor_span.end=end->end;
 if(r->upload_count&&(!same(&r->model_span,&r->upload_span)||r->upload_span.begin<r->model_span.begin||r->upload_span.end>r->model_span.end||r->upload_span.end<=r->upload_span.begin))return bad(r,VAB_ORDER);
 
 if(!same(&r->actor_span,&r->model_span)||r->model_span.begin<end->begin||r->model_span.end>end->end||r->model_span.end<=r->model_span.begin)return bad(r,VAB_ORDER);
 r->status=stamped(&r->stamp)?VAB_OK:VAB_UNBOUND;
 return r->status;
 }
int vab_match_draw(const VabRecord*r,const VabStamp*s,const VabSpan*pb,uint64_t serial,uint32_t program,uint32_t constants){if(!r||!s||!pb||r->status!=VAB_OK||!serial||!program||!constants||!stamped(s))return VAB_UNBOUND;
 
 if(memcmp(&r->stamp,s,sizeof *s)||!same(&r->model_span,pb)||pb->begin<r->model_span.begin||pb->end>r->model_span.end||pb->end<=pb->begin)return VAB_UNBOUND;
 return VAB_OK;
 }

int vab_bind_owned(VabRecord*r,const VabStamp*s){
 if(!r||!s||r->status!=VAB_UNBOUND||r->stamp.frame_serial||r->stamp.device_epoch||!stamped(s)||r->stamp.scene!=s->scene||r->stamp.view!=s->view||r->stamp.simulation_tick!=s->simulation_tick||r->actor_span.end<=r->actor_span.begin||r->model_span.end<=r->model_span.begin)return VAB_UNBOUND;
 r->stamp=*s;r->status=VAB_OK;return VAB_OK;
}

int vab_actor_begin(const VabReader*p,uint32_t owner,uint32_t actor,uint32_t caller,uint32_t replay,const VabStamp*s,const VabSpan*b,VabRecord*r){return actor_begin(p,owner,actor,caller,replay,0,s,b,r);}
int vab_actor_begin_current_owner(const VabReader*p,uint32_t owner,uint32_t actor,uint32_t caller,const VabStamp*s,const VabSpan*b,VabRecord*r){return actor_begin(p,owner,actor,caller,0,1,s,b,r);}
