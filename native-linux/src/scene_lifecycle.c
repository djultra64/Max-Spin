/* Bounded host registry; no address-only generations. Own work: djultra64. */
#include "scene_lifecycle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
typedef struct {uint32_t va,kind,live,view_va;uint64_t id,view_id;} Record;
static Record records[TSL_MAX_LIVE];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_int on;
static atomic_uint_fast64_t authority;
static int configured,failed;
static uint64_t serial;
static TslRead reader;static void*reader_user;
static _Thread_local int callback,reentered;
static int enter(void){if(callback){reentered=1;failed=1;atomic_store_explicit(&authority,0,memory_order_release);return 0;}return atomic_load_explicit(&on,memory_order_acquire);}
int tsl_enabled(void){return atomic_load_explicit(&on,memory_order_acquire);}
uint64_t tsl_authority_revision(void){return atomic_load_explicit(&authority,memory_order_acquire);}
/* Called with lock held. Zero is a permanent invalid sentinel, never a wrapped
 * revision that could accidentally authorize an old consumer. */
static int bump(void){uint64_t current=atomic_load_explicit(&authority,memory_order_relaxed);
 if(!current||current==UINT64_MAX){failed=1;atomic_store_explicit(&authority,0,memory_order_release);return 0;}
 atomic_store_explicit(&authority,current+1,memory_order_release);return 1;
}
static int span(uint32_t va,size_t bytes){return va>=0x10000u&&(uint64_t)va+bytes<=0x4000000u;}
static int word(uint32_t va,uint32_t*out){
 if(!span(va,4)||!reader)return 0;
 callback=1;reentered=0;int ok=reader(reader_user,va,out,4);callback=0;
 return ok&&!reentered;
}
static Record*find(uint32_t kind,uint32_t va){for(unsigned i=0;i<TSL_MAX_LIVE;i++)if(records[i].id&&records[i].kind==kind&&records[i].va==va)return &records[i];return NULL;}
static void retire(uint32_t kind,uint32_t va){Record*r=find(kind,va);if(r)r->live=0;}
int tsl_configure(int enabled,TslRead read,void*user){
 if(callback){reentered=1;failed=1;atomic_store_explicit(&authority,0,memory_order_release);return 0;}if((enabled!=0&&enabled!=1)||(enabled&&!read))return 0;
 pthread_mutex_lock(&lock);if(configured){pthread_mutex_unlock(&lock);return 0;}configured=1;reader=read;reader_user=user;
 if(enabled)atomic_store_explicit(&authority,1,memory_order_release);
 atomic_store_explicit(&on,enabled,memory_order_release);pthread_mutex_unlock(&lock);return 1;
}
uint64_t tsl_ctor_begin(uint32_t kind,uint32_t va){
 if(!enter())return 0;pthread_mutex_lock(&lock);
 if((kind!=TSL_APP&&kind!=TSL_VIEW)||!span(va,kind==TSL_APP?0x640c:0x1f8)||failed){pthread_mutex_unlock(&lock);return 0;}
 if(!bump()){pthread_mutex_unlock(&lock);return 0;}
 /* A new constructor is the only authority to assign a new lifetime to a
  * reused address. Retired records can be reused because IDs never repeat. */
 for(unsigned i=0;i<TSL_MAX_LIVE;i++)if(records[i].va==va)records[i].live=0;
 Record*r=find(kind,va);
 if(!r)for(unsigned i=0;i<TSL_MAX_LIVE;i++)if(!records[i].live){r=&records[i];break;}
 if(!r||serial==UINT64_MAX){failed=1;pthread_mutex_unlock(&lock);return 0;}
 uint64_t id=++serial;*r=(Record){va,kind,2,0,id,0};pthread_mutex_unlock(&lock);return id;
}
int tsl_ctor_end(uint32_t kind,uint32_t va,uint64_t ticket,uint32_t result){
 if(!enter())return 0;pthread_mutex_lock(&lock);int ok=0;Record*r=find(kind,va);uint32_t vtable;
 if(failed||!ticket||!r||r->id!=ticket||r->live!=2||result!=va)goto done;
 if(!word(va,&vtable)||vtable!=(kind==TSL_APP?0x2366fcu:0x236940u))goto done;
 if(kind==TSL_APP){uint32_t owner,ovt;if(!word(va+0x6408,&owner))goto done;Record*v=find(TSL_VIEW,owner);
  if(!v||v->live!=1||!word(owner,&ovt)||ovt!=0x236940u)goto done;
  r->view_va=owner;r->view_id=v->id;
 }
 if(!bump())goto done;r->live=1;ok=1;
done:if(!ok&&r&&r->id==ticket){r->live=0;if(!failed)(void)bump();}pthread_mutex_unlock(&lock);return ok;
}
void tsl_dtor(uint32_t kind,uint32_t va){if(!enter())return;pthread_mutex_lock(&lock);
 if((kind==TSL_APP||kind==TSL_VIEW)&&span(va,4)&&!failed){(void)bump();retire(kind,va);}pthread_mutex_unlock(&lock);
}
int tsl_snapshot(uint32_t va,TslSnapshot*out){
 if(out)memset(out,0,sizeof *out);if(!out||!enter())return 0;
 pthread_mutex_lock(&lock);int ok=0;uint32_t vt,owner,ovt,tick,time_bits;Record*a=find(TSL_APP,va);
 if(failed||!a||a->live!=1||!word(va,&vt)||vt!=0x2366fcu||!word(va+0x6408,&owner)||owner!=a->view_va)goto done;
 Record*v=find(TSL_VIEW,owner);
 if(!v||v->live!=1||v->id!=a->view_id||!word(owner,&ovt)||ovt!=0x236940u)goto done;
 if(!word(0x413284,&tick)||!word(0x413280,&time_bits))goto done;
 *out=(TslSnapshot){TSL_VERSION,va,owner,0,a->id,v->id,tsl_authority_revision(),tick,time_bits};ok=out->authority_revision!=0;
done:pthread_mutex_unlock(&lock);return ok;
}

int tsl_live_owner(uint32_t va,uint64_t scene,uint64_t view,uint64_t revision){
 if(!va||!scene||!view||!revision||!enter())return 0;
 pthread_mutex_lock(&lock);int ok=0;
 if(failed||atomic_load_explicit(&authority,memory_order_acquire)!=revision)goto done;
 Record*v=find(TSL_VIEW,va);
 if(!v||v->live!=1||v->id!=view)goto done;
 for(unsigned i=0;i<TSL_MAX_LIVE;i++){
  const Record*a=records+i;
  if(a->kind==TSL_APP&&a->live==1&&a->id==scene&&a->view_va==va&&a->view_id==view){ok=1;break;}
 }
done:pthread_mutex_unlock(&lock);return ok;
}
