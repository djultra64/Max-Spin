#include "constant_word_lineage.h"
#include <string.h>
static int enabled(const VclLedger*l){return l&&l->version==1&&l->enabled&&!l->failed&&l->owner&&l->owner(l->user);}
static int tag(const VpliSceneTag*t){return t&&t->version==1&&!t->reserved&&t->scene&&t->view&&t->authority_revision&&t->boundary_ns;}
static int span(const VplSpan*s){return s&&s->identity&&s->epoch&&s->begin<s->end&&!((s->begin|s->end)&3);}
int vcl_configure(VclLedger*l,int on,void*u,int(*owner)(void*)){if(!l||l->version||(on!=0&&on!=1)||(on&&!owner))return 0;memset(l,0,sizeof*l);l->version=1;l->enabled=on;l->generation=1;l->user=u;l->owner=owner;return 1;}
void vcl_invalidate(VclLedger*l){if(!enabled(l))return;l->load_known=0;l->pending=0;l->latest_revision=0;if(l->generation==UINT64_MAX){l->failed=1;return;}l->generation++;}
void vcl_invalidate_rows(VclLedger*l,uint32_t first,uint32_t rows){if(!enabled(l))return;if(!rows||first>=192||rows>192-first){vcl_invalidate(l);return;}for(unsigned i=first*4;i<(first+rows)*4;i++)l->lanes[i].generation=0;}
int vcl_method(VclLedger*l,uint32_t method,uint32_t word,const VpliPacket*p){
 if(!enabled(l))return 0;
 if(!p||p->version!=VPLI_PACKET_VERSION||p->method!=method||!span(&p->packet)||p->packet.end-p->packet.begin<8||!tag(&p->scene)){vcl_invalidate(l);return 0;}
 if(l->pending){vcl_invalidate(l);return 0;}
 if(method==0x1ea4){l->load=word;l->component=0;l->load_known=1;return 1;}
 if(method<0xb80||method>=0xc00)return 1;
 if(!l->load_known){vcl_invalidate(l);return 0;}
 if(l->load<192){l->pending=1;l->pending_lane=l->load*4+l->component;l->pending_word=word;l->pending_span=p->packet;l->pending_span.begin=l->pending_span.end-4;l->pending_source=p->scene;}
 /* Exact uint32 wrapping semantics of actual nv2a_vsh_constant_word. */
 if(++l->component==4){l->component=0;l->load++;}return 1;
}
int vcl_written_by(const VclLedger*l,uint32_t first,uint32_t rows,const VplSpan*upload,const VpliSceneTag*source){
 if(!enabled(l)||l->pending||!rows||first>=192||rows>192-first||!span(upload)||!tag(source))return 0;
 for(unsigned i=first*4;i<(first+rows)*4;i++){const VclWord*w=&l->lanes[i];if(w->generation!=l->generation||!vpl_contains(upload,&w->word)||memcmp(&w->source,source,sizeof*source))return 0;}return 1;
}

int vcl_written(VclLedger*l,uint32_t row,uint32_t component,uint32_t word,uint32_t revision){
 if(!enabled(l))return 0;
 if(!l->pending||row>=192||component>=4||row*4+component!=l->pending_lane||word!=l->pending_word||!revision||(l->latest_revision&&revision!=l->latest_revision+1)){vcl_invalidate(l);return 0;}
 VclWord*w=&l->lanes[l->pending_lane];w->generation=l->generation;w->word=l->pending_span;w->source=l->pending_source;w->revision=revision;l->latest_revision=revision;l->pending=0;return 1;
}
int vcl_snapshot(const VclLedger*l,uint32_t revision,VclSnapshot*out){
 if(!enabled(l)||!out||!revision||revision!=l->latest_revision||l->pending)return 0;
 VclSnapshot s={0};s.version=1;s.revision=revision;
 for(unsigned i=0;i<VCL_LANES;i++){
  const VclWord*w=&l->lanes[i];if(w->generation!=l->generation)continue;
  VclRun*r=s.count?&s.runs[s.count-1]:NULL;
  if(r&&r->first+r->count==i&&r->first_revision+r->count==w->revision&&r->words.identity==w->word.identity&&r->words.epoch==w->word.epoch&&r->words.wrap_generation==w->word.wrap_generation&&r->words.end==w->word.begin&&!memcmp(&r->source,&w->source,sizeof w->source)){r->count++;r->words.end=w->word.end;}
  else {r=&s.runs[s.count++];*r=(VclRun){i,1,w->revision,0,w->word,w->source};}
 }
 if(!s.count)return 0;*out=s;return 1;
}
size_t vcl_snapshot_bytes(const VclSnapshot*s){if(!s||s->version!=1||s->reserved||!s->revision||!s->count||s->count>VCL_LANES)return 0;return offsetof(VclSnapshot,runs)+(size_t)s->count*sizeof*s->runs;}
int vcl_snapshot_written_by(const VclSnapshot*s,uint32_t first,uint32_t rows,const VplSpan*upload,const VpliSceneTag*source){
 if(!vcl_snapshot_bytes(s)||!rows||first>=192||rows>192-first||!span(upload)||!tag(source))return 0;
 uint32_t lane=first*4,end=(first+rows)*4,previous=0;
 for(unsigned i=0;i<s->count;i++){
  const VclRun*r=&s->runs[i];
  if(r->reserved||!r->count||r->first>=768||r->count>768-r->first||r->first<previous||!r->first_revision||r->first_revision>s->revision||r->count-1>s->revision-r->first_revision||!span(&r->words)||r->words.end-r->words.begin!=4*r->count||!tag(&r->source))return 0;
  previous=r->first+r->count;
  if(lane<r->first)continue;if(lane>=previous)continue;
  uint32_t take=previous<end?previous:end;VplSpan part=r->words;part.begin+=4*(lane-r->first);part.end=part.begin+4*(take-lane);
  if(!vpl_contains(upload,&part)||memcmp(&r->source,source,sizeof*source))return 0;lane=take;
 }
 return lane==end;
}
