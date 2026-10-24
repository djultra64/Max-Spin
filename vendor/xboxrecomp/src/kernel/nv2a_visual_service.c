/* Host visual work only, no renderer or guest dependencies. djultra64. */
#include "nv2a_visual_service.h"
#include <stdatomic.h>
#include <stddef.h>
static Nv2aVisualService service;
static int configured,enabled;
static atomic_int started,revoked;
static atomic_uint_fast64_t revision;
/* Actual executor owner only after start. */
static int online,busy,stopped,have_time;
static uint64_t observed_revision,deadline,last_time;
int nv2a_visual_configure(const Nv2aVisualService*s,int on){
 if(configured||atomic_load_explicit(&started,memory_order_acquire)||(on!=0&&on!=1))return 0;
 if(on&&(!s||!s->owner||!s->now_ns||!s->run||!s->stop))return 0;
 if(on){service=*s;}
 configured=1;enabled=on;return 1;
}
int nv2a_visual_start(void){
 if(atomic_exchange_explicit(&started,1,memory_order_acq_rel))return 0;
 if(!enabled||!service.owner(service.user))return 0;
 online=1;deadline=0;return 1;
}
void nv2a_visual_revoke(void){atomic_store_explicit(&revoked,1,memory_order_release);}
void nv2a_visual_notify(void){
 if(!enabled||atomic_load_explicit(&revoked,memory_order_acquire))return;
 uint_fast64_t old=atomic_load_explicit(&revision,memory_order_relaxed);
 for(;;){
  if(old==UINT64_MAX){nv2a_visual_revoke();return;}
  if(atomic_compare_exchange_weak_explicit(&revision,&old,old+1,memory_order_release,memory_order_relaxed))return;
 }
}
static int leave_poll(int result){busy=0;return result;}
int nv2a_visual_poll(int idle){
 if(!enabled||!online||busy||!idle||atomic_load_explicit(&revoked,memory_order_acquire))return 0;
 busy=1;
 if(!service.owner(service.user)||atomic_load_explicit(&revoked,memory_order_acquire))return leave_poll(0);
 uint64_t changed=atomic_load_explicit(&revision,memory_order_acquire);
 if(changed!=observed_revision){observed_revision=changed;deadline=0;}
 if(deadline==UINT64_MAX)return leave_poll(0);
 uint64_t now=service.now_ns(service.user);
 if(atomic_load_explicit(&revoked,memory_order_acquire))return leave_poll(0);
 if(have_time&&now<last_time){nv2a_visual_revoke();return leave_poll(0);}
 last_time=now;have_time=1;
 if(now<deadline)return leave_poll(0);
 uint64_t next=UINT64_MAX;int ok=service.run(service.user,now,&next);
 if(ok!=1){nv2a_visual_revoke();return leave_poll(0);}
 if(next!=UINT64_MAX&&next<=now){if(now==UINT64_MAX){nv2a_visual_revoke();return leave_poll(0);}next=now+1;}
 deadline=next;return leave_poll(1);
}
void nv2a_visual_stop(void){
 if(!enabled||!online||busy||stopped)return;
 busy=1;
 if(!service.owner(service.user)){busy=0;return;}
 stopped=1;online=0;nv2a_visual_revoke();service.stop(service.user);busy=0;
}

int nv2a_visual_enabled(void){return enabled&&online&&!atomic_load_explicit(&revoked,memory_order_acquire);}
