/* Original source topology union. Own work djultra64. */
#include "material_save_sources.h"
#include <string.h>
#include "material_export_tables.h"
typedef struct {McReadWord rd;void*user;McSaveSources out;uint32_t nodes[MC_SAVE_NODES],props[MC_SAVE_NODES];unsigned nn,np;unsigned char ns[MC_SAVE_NODES],ps[MC_SAVE_NODES];McSaveSourcesDiagnostic*d;} Scan;
static int bad(Scan*s,unsigned stage){if(s->d&&!s->d->stage){s->d->stage=stage;s->d->nodes=s->nn;s->d->properties=s->np;}return 0;}
static int word(Scan*s,uint32_t a,uint32_t*v){if(s->d){s->d->address=a;s->d->value=0;}if(a<0x10000u||a>0x3fffffcu)return bad(s,MCI_READ_RANGE);if(!s->rd(s->user,a,v))return bad(s,MCI_READ_CALLBACK);if(s->d)s->d->value=*v;uint32_t p[2]={a,*v};for(unsigned i=0;i<8;i++){s->out.fingerprint^=((const unsigned char*)p)[i];s->out.fingerprint*=1099511628211ull;}return 1;}
static int add(Scan*s,uint32_t r){if(!r)return 1;for(unsigned i=0;i<s->out.count;i++)if(s->out.resources[i]==r)return 1;if(s->out.count==MC_SAVE_RESOURCES)return bad(s,MCI_RESOURCE_LIMIT);s->out.resources[s->out.count++]=r;return 1;}
static int property(Scan*s,uint32_t p,unsigned depth){
 if(!p)return 1;
 if(s->d){s->d->property=p;s->d->depth=depth;}
 if(depth>32)return bad(s,MCI_PROPERTY_DEPTH);
 for(unsigned i=0;i<s->np;i++)if(s->props[i]==p)return s->ps[i]==2?1:bad(s,MCI_PROPERTY_CYCLE);
 if(s->np==MC_SAVE_NODES)return bad(s,MCI_PROPERTY_LIMIT);
 unsigned at=s->np++;s->props[at]=p;s->ps[at]=1;
 uint32_t inherited,texture,a,b;
 if(!word(s,p+0xc,&inherited)||!word(s,p+0x28,&texture)||!word(s,p+0x1c,&a)||!word(s,p+0x20,&b))return 0;
 /* Include ancestor even when overridden: variants may refer back to it. */
 int ok=add(s,texture)&&property(s,inherited,depth+1)&&property(s,a,depth+1)&&property(s,b,depth+1);
 if(ok)s->ps[at]=2;
 return ok;
}
static int descriptor(Scan*s,uint32_t d){
 uint32_t seen[33];unsigned n=0;
 while(d){if(s->d)s->d->descriptor=d;if(n==33)return bad(s,MCI_DESCRIPTOR_LIMIT);for(unsigned i=0;i<n;i++)if(seen[i]==d)return bad(s,MCI_DESCRIPTOR_CYCLE);seen[n++]=d;uint32_t next;if(!word(s,d+0x2c,&next))return 0;if(next){d=next;continue;}uint32_t p;if(!word(s,d+0x18,&p))return 0;return property(s,p,0);}
 return 1;
}
static int group(Scan*s,uint32_t g){
 if(!g)return 1;
 uint32_t n,a;if(!word(s,g+8,&n)||!word(s,g+0xc,&a))return 0;if(s->d)s->d->count=n;if(n>MC_SAVE_NODES)return bad(s,MCI_GROUP_COUNT);
 for(uint32_t i=0;i<n;i++){uint32_t d;if(!word(s,a+i*4,&d)||!descriptor(s,d))return 0;}return 1;
}
static int node(Scan*s,uint32_t n,unsigned depth){
 if(!n)return 1;
 if(s->d){s->d->node=n;s->d->depth=depth;s->d->vtable=0;s->d->save_callback=0;}
 if(depth>32)return bad(s,MCI_NODE_DEPTH);
 /* A repeated entity may be a shared child, not necessarily a cycle. The
  * depth bound still rejects a cyclic graph rather than unbounded recursion. */
 for(unsigned i=0;i<s->nn;i++)if(s->nodes[i]==n)return s->ns[i]==2?1:bad(s,MCI_NODE_CYCLE);
 if(s->nn==MC_SAVE_NODES)return bad(s,MCI_NODE_LIMIT);
 unsigned at=s->nn++;s->nodes[at]=n;s->ns[at]=1;
 uint32_t vt,save,children,count,g;
 if(!word(s,n,&vt))return 0;if(s->d)s->d->vtable=vt;
 if(!word(s,vt+0xcc,&save))return 0;if(s->d)s->d->save_callback=save;
 if(save!=0xcc710u){
  /* Actual Creator actor1EDA78 exports MATERIAL through phase+C4 (CC710)
   * before its separate +CC C31B0 phase. Original C48D0 calls both phases.
   * Do not treat C31B0 itself as a generic material exporter. */
  uint32_t material=0;
  if(!material_phase_table(vt,save)||!word(s,vt+0xc4,&material)||material!=0xcc710u)return bad(s,MCI_SAVE_CALLBACK);
 }
 if(!word(s,n+0x58,&g)||!group(s,g)||!word(s,n+4,&count)||!word(s,n+8,&children))return 0;
 if(s->d)s->d->count=count;if(count>MC_SAVE_NODES)return bad(s,MCI_CHILD_COUNT);
 for(uint32_t i=0;i<count;i++){uint32_t c;if(!word(s,children+i*4,&c)||!node(s,c,depth+1))return 0;}
 s->ns[at]=2;return 1;
}
int mc_save_sources_diagnostic(McReadWord rd,void*user,uint32_t model,McSaveSources*out,McSaveSourcesDiagnostic*d){
 if(!rd||!out)return 0;
 Scan s;memset(&s,0,sizeof s);s.rd=rd;s.user=user;s.d=d;if(d)memset(d,0,sizeof*d);s.out.fingerprint=14695981039346656037ull;
 uint32_t n,a;if(!word(&s,model+8,&n)||!word(&s,model+0xc,&a))return 0;if(d)d->count=n;if(n>MC_SAVE_NODES)return bad(&s,MCI_MODEL_COUNT);
 for(uint32_t i=0;i<n;i++){uint32_t p;if(!word(&s,a+i*4,&p)||!node(&s,p,0))return 0;}*out=s.out;return 1;
}

int mc_save_sources(McReadWord rd,void*user,uint32_t model,McSaveSources*out){return mc_save_sources_diagnostic(rd,user,model,out,NULL);}
