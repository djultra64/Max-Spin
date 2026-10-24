/* No guest writes, input injection, or SDL polling. Own work: djultra64. */
#include "creator_input_observer.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <fenv.h>
#include "material_guest_cpu.h"
#include "input/input_host.h"
extern void sub_001D74A2_gen(void);
extern void sub_000B9750_gen(void);
static int enabled;
static atomic_uint records;
static _Thread_local int observing;
typedef struct {
 uint32_t phase,caller,entry,returned,handle,out,base,status,active,valid;
 uint32_t host_status,host_packet,raw_packet;
 uint16_t host_buttons,held,rising;
 uint8_t host_pad[18],raw[22],before[22];
} Observation;
typedef struct {int used;Observation value;} Prior;
static _Thread_local Prior prior[8];
int topspin_creator_input_observer_configure(void){
 const char*v=getenv("TOPSPIN_CREATOR_INPUT_DIAGNOSTICS");enabled=v&&strcmp(v,"1")==0;
 atomic_store_explicit(&records,0,memory_order_relaxed);memset(prior,0,sizeof prior);return enabled;
}
static int read_guest(uint32_t a,void*out,size_t n){
 if(a<0x10000u||(uint64_t)a+n>0x4000000u)return 0;
 memcpy(out,(const void*)XBOX_PTR(a),n);return 1;
}
static void host_pad(Observation*r){
 XBOX_INPUT_STATE s;memset(&s,0,sizeof s);r->host_status=xbox_HostInputGetState(0,&s);
 r->host_packet=s.dwPacketNumber;r->host_buttons=s.Gamepad.wButtons;
 /* Encode fields explicitly: native struct tail padding is not guest ABI. */
 memcpy(r->host_pad,&s.Gamepad.wButtons,2);memcpy(r->host_pad+2,s.Gamepad.bAnalogButtons,8);
 memcpy(r->host_pad+10,&s.Gamepad.sThumbLX,2);memcpy(r->host_pad+12,&s.Gamepad.sThumbLY,2);
 memcpy(r->host_pad+14,&s.Gamepad.sThumbRX,2);memcpy(r->host_pad+16,&s.Gamepad.sThumbRY,2);
}
static void hex(const uint8_t*p,size_t n){for(size_t i=0;i<n;i++)fprintf(stderr,"%02x",p[i]);}
static void publish(Observation*r){
 host_pad(r);
 Observation key=*r;key.returned-=key.entry;key.entry=0;key.host_packet=key.raw_packet=0;
 memset(key.raw,0,4);memset(key.before,0,4);
 unsigned slot=0;for(;slot<8;slot++)if(prior[slot].used&&prior[slot].value.phase==key.phase&&prior[slot].value.out==key.out)break;
 if(slot==8){for(slot=0;slot<8&&prior[slot].used;slot++);if(slot==8)slot=(key.out>>1)%8;}
 if(prior[slot].used&&!memcmp(&prior[slot].value,&key,sizeof key))return;
 prior[slot].used=1;prior[slot].value=key;
 unsigned n=atomic_load_explicit(&records,memory_order_relaxed);
 do{if(n>=64)return;}while(!atomic_compare_exchange_weak_explicit(&records,&n,n+1,memory_order_relaxed,memory_order_relaxed));
 fprintf(stderr,"[CREATOR-INPUT] n=%u seam=%s caller=%08x esp=%08x->%08x ret_ok=%u handle=%08x out=%08x base=%08x status=%08x valid=%02x active_control=%08x HOSTslot0_metadata status=%08x packet=%u buttons=%04x pad=",n+1,r->phase==1?"GetState":"Poll-post",r->caller,r->entry,r->returned,r->returned==r->entry+(r->phase==1?12u:4u),r->handle,r->out,r->base,r->status,r->valid,r->active,r->host_status,r->host_packet,r->host_buttons);
 hex(r->host_pad,18);fprintf(stderr," guest_packet=%u raw22=",r->raw_packet);hex(r->raw,22);
 fprintf(stderr," before22=");hex(r->before,22);fprintf(stderr," port0_held=%04x port0_rising=%04x\n",r->held,r->rising);
}
void sub_001D74A2(void){
 if(!enabled||observing){sub_001D74A2_gen();return;}
 Observation r={0};Cpu saved;cpu_save(&saved);observing=1;
 r.phase=1;r.entry=g_esp;
 if(read_guest(g_esp,&r.caller,4)&&read_guest(g_esp+4,&r.handle,4)&&read_guest(g_esp+8,&r.out,4)){
  r.valid|=1;if(read_guest(r.out,r.before,22))r.valid|=2;
 }
 observing=0;cpu_restore(&saved);sub_001D74A2_gen();
 cpu_save(&saved);observing=1;r.returned=g_esp;r.status=g_eax;
 if((r.valid&1)&&read_guest(r.out,r.raw,22)){r.valid|=4;memcpy(&r.raw_packet,r.raw,4);}
 if(read_guest(0x2B8B20u,&r.active,4))r.valid|=8;
 publish(&r);observing=0;cpu_restore(&saved);
}
void sub_000B9750(void){
 if(!enabled||observing){sub_000B9750_gen();return;}
 Observation r={0};Cpu saved;cpu_save(&saved);observing=1;r.phase=2;r.entry=g_esp;r.out=0x2B8B78u;
 if(read_guest(g_esp,&r.caller,4)&&read_guest(g_esp+4,&r.base,4)){r.valid|=1;if(!r.base)r.base=0x2B8BD0u;}
 observing=0;cpu_restore(&saved);sub_000B9750_gen();
 cpu_save(&saved);observing=1;r.returned=g_esp;r.status=g_eax;
 if(read_guest(r.out,r.raw,22)){r.valid|=4;memcpy(&r.raw_packet,r.raw,4);}
 if((r.valid&1)&&(uint64_t)r.base+0xD4<=0x4000000u&&r.base>=0x10000u){
  if(read_guest(r.base+0xD0,&r.handle,4)&&read_guest(r.base+0x24,&r.held,2)&&read_guest(r.base+0x48,&r.rising,2))r.valid|=16;
 }
 if(read_guest(0x2B8B20u,&r.active,4))r.valid|=8;
 publish(&r);observing=0;cpu_restore(&saved);
}
