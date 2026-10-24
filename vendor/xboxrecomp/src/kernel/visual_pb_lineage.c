/* Bounded exact-copy ledger. Own work: djultra64. */
#include "visual_pb_lineage.h"
#include <limits.h>
#include <string.h>
static int bad(VplLedger*l,unsigned s){l->status=s;return 0;}
static int mismatch(VplLedger*l,uint32_t kind,uint32_t original,uint32_t canonical,
 uint32_t expected,uint32_t observed,const VplSpan*span){
 if(!l->first_mismatch.kind){
  l->first_mismatch.original_va=original;l->first_mismatch.canonical=canonical;
  l->first_mismatch.expected=expected;l->first_mismatch.observed=observed;
  l->first_mismatch.span=*span;l->first_mismatch.kind=kind;
 }
 return bad(l,VPL_BYTES_CHANGED);
}
static int ready(VplLedger*l){if(l&&l->busy)return bad(l,VPL_ORDER);return l&&l->status==VPL_OK;}
static uint64_t identity(const VplLedger*l){return((uint64_t)l->base<<32)|l->size;}
static int resolve(VplLedger*l,uint32_t at,size_t n,uint32_t*out){
 uint32_t canonical=0,base=0,size=0;uint64_t epoch=0;
 l->busy=1;int ok=l->reader.allocation(l->reader.user,at,n,&canonical,&base,&size,&epoch);l->busy=0;if(l->status!=VPL_OK)return 0;
 if(!ok||base!=l->base||size!=l->size||epoch!=l->epoch||canonical<l->base||(uint64_t)canonical+n>(uint64_t)base+size)return bad(l,VPL_EPOCH);
 *out=canonical;return 1;
}
int vpl_begin(VplLedger*l,const VplReader*r,uint32_t begin,uint32_t end,uint32_t put){
 if(!l)return 0;if(l->initialized)return bad(l,VPL_ORDER);if(!r||!r->enabled)return 0;memset(l,0,sizeof*l);l->initialized=1;
 if(r->enabled!=1||!r->allocation||!r->read)return bad(l,VPL_INVALID);
 l->reader=*r;l->status=VPL_OK;uint32_t canonical=0;
 if(begin>=end||((begin|end|put)&3))return bad(l,VPL_INVALID);
 l->busy=1;int ok=r->allocation(r->user,begin,4,&canonical,&l->base,&l->size,&l->epoch);l->busy=0;
 if(l->status!=VPL_OK)return 0;
 if(!ok||!l->size||!l->epoch)return bad(l,VPL_INVALID);
 l->status=VPL_OK;l->ring_begin=canonical;
 uint32_t last=0;if(!resolve(l,end-4,4,&last)||last<canonical)return 0;
 l->ring_end=last+4;if(l->ring_end<last)return bad(l,VPL_INVALID);
 uint64_t ring_bytes=(uint64_t)l->ring_end-l->ring_begin;
 if(!ring_bytes||ring_bytes>VPL_MAX_RING_BYTES)return bad(l,VPL_BUDGET);
 l->interval_limit=(uint32_t)((ring_bytes+VPL_BYTES-1u)/VPL_BYTES)+VPL_TAG_CUTS+2u;
 if(!resolve(l,put,4,&l->producer)||l->producer<l->ring_begin||l->producer>=l->ring_end)return bad(l,VPL_ORDER);
 /* First observed incarnation starts0, NOT a claim of title's prior wraps.
  * Only spans recorded after attach can be matched. */
 return 1;
}
static int publish(VplLedger*l,uint32_t begin,uint32_t end,VplSpan*out,int append){
 if(!ready(l))return 0;uint32_t a=0,b=0;
 if(begin>=end||((begin|end)&3)||!resolve(l,begin,4,&a)||!resolve(l,end-4,4,&b))return ready(l)?bad(l,VPL_INVALID):0;
 if(b>UINT32_MAX-4u)return bad(l,VPL_INVALID);b+=4;
 if(a!=l->producer||a<l->ring_begin||b>l->ring_end||b<=a)return bad(l,VPL_ORDER);
 VplInterval*q=NULL;uint32_t offset=0;
 if(append){
  if(!l->count)return bad(l,VPL_ORDER);q=&l->intervals[(l->head+l->count-1u)%VPL_INTERVALS];
  if(q->span.identity!=identity(l)||q->span.epoch!=l->epoch||q->span.wrap_generation!=l->wrap||q->span.end!=a||q->used>VPL_BYTES)return bad(l,VPL_ORDER);
  offset=q->used;if(b-a>VPL_BYTES-offset)return bad(l,VPL_BUDGET);
 }else if(b-a>VPL_BYTES||l->count>=l->interval_limit)return bad(l,VPL_BUDGET);
 for(unsigned i=0;i<l->count;i++){VplInterval*r=&l->intervals[(l->head+i)%VPL_INTERVALS];if(a<r->span.end&&b>r->span.begin+r->consumed)return bad(l,VPL_OVERWRITE);}
 if(!append){q=&l->intervals[(l->head+l->count)%VPL_INTERVALS];memset(q,0,sizeof*q);q->span=(VplSpan){identity(l),l->epoch,l->wrap,a,a};}
 l->busy=1;int copied=l->reader.read(l->reader.user,a,q->bytes+offset,b-a);l->busy=0;if(l->status!=VPL_OK)return 0;
 if(!copied||!resolve(l,a,b-a,&a))return bad(l,VPL_EPOCH);
 VplSpan emitted={identity(l),l->epoch,l->wrap,a,b};
 q->used=offset+b-a;q->span.end=b;l->producer=b;if(!append)l->count++;if(out)*out=emitted;return 1;
}
int vpl_publish(VplLedger*l,uint32_t begin,uint32_t end,VplSpan*out){return publish(l,begin,end,out,0);}
int vpl_append(VplLedger*l,uint32_t begin,uint32_t end,VplSpan*out){return publish(l,begin,end,out,1);}
static int wrap(VplLedger*l,uint32_t at,uint32_t word,uint32_t reset,int append){
 if(!ready(l))return 0;uint32_t a=0,to=0;
 if(l->depth||!resolve(l,at,4,&a)||!resolve(l,reset,4,&to))return ready(l)?bad(l,VPL_ORDER):0;
 if(to!=l->ring_begin||a!=l->producer||a>UINT32_MAX-4u||word!=((l->ring_begin&0x0fffffffu)|1u)||l->wrap==UINT64_MAX)return bad(l,VPL_ORDER);
 VplSpan ignored;if(!publish(l,at,at+4,&ignored,append))return 0;
 VplInterval*q=&l->intervals[(l->head+l->count-1u)%VPL_INTERVALS];uint32_t copied;
 /* Exact jump offset even when appended to existing immutable bytes. */
 memcpy(&copied,q->bytes+(a-q->span.begin),4);if(copied!=word)return mismatch(l,VPL_MISMATCH_WRAP,at,a,word,copied,&q->span);
 l->wrap++;l->producer=to;return 1;
}
int vpl_wrap(VplLedger*l,uint32_t at,uint32_t word,uint32_t reset){return wrap(l,at,word,reset,0);}
int vpl_wrap_append(VplLedger*l,uint32_t at,uint32_t word,uint32_t reset){return wrap(l,at,word,reset,1);}
int vpl_window_begin(VplLedger*l){if(!ready(l))return 0;if(l->depth==VPL_DEPTH)return bad(l,VPL_BUDGET);l->windows[l->depth++]=(VplSpan){identity(l),l->epoch,l->wrap,l->producer,l->producer};return 1;}
int vpl_window_end(VplLedger*l,VplSpan*out){if(!ready(l))return 0;if(!l->depth||!out)return bad(l,VPL_ORDER);VplSpan s=l->windows[--l->depth];if(s.wrap_generation!=l->wrap||s.end!=s.begin||l->producer<=s.begin)return bad(l,VPL_ORDER);s.end=l->producer;*out=s;return 1;}
int vpl_consume(VplLedger*l,uint32_t at,uint32_t value,VplSpan*out){
 if(!ready(l))return 0;uint32_t a=0;if(!resolve(l,at,4,&a))return 0;
 if(!l->count)return bad(l,VPL_ORDER);VplInterval*q=&l->intervals[l->head];
 if(a!=q->span.begin+q->consumed)return bad(l,VPL_ORDER);
 uint32_t expected;memcpy(&expected,q->bytes+q->consumed,4);if(value!=expected)return mismatch(l,VPL_MISMATCH_CONSUME,at,a,expected,value,&q->span);
 if(out){*out=q->span;out->begin=a;out->end=a+4;}
 q->consumed+=4;if(q->consumed==q->used){memset(q,0,sizeof*q);l->head=(l->head+1)%VPL_INTERVALS;l->count--;}
 return 1;
}
int vpl_contains(const VplSpan*p,const VplSpan*c){return p&&c&&p->identity&&p->epoch&&p->begin<p->end&&c->begin<c->end&&!((p->begin|p->end|c->begin|c->end)&3)&&p->identity==c->identity&&p->epoch==c->epoch&&p->wrap_generation==c->wrap_generation&&p->begin<=c->begin&&p->end>=c->end&&c->begin<c->end;}
