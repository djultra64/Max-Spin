/* Executes on actual PB decoder owner; no guest reads in SOURCE. djultra64. */
#include "visual_pb_hooks.h"
VpliBridge *vpli_enabled_bridge;
static VpliBridge*bridge;static VpliProgramSink sink;static void*sink_user;
static VpliMethodObserver method_observer;static void*method_user;
int vpli_method_observer_install(VpliMethodObserver observer,void*user){if(method_observer||!observer)return 0;method_user=user;method_observer=observer;return 1;}
int vpli_hooks_install(VpliBridge*b,VpliProgramSink s,void*u){if(bridge||!b||!b->configured||!b->enabled||!s)return 0;bridge=b;vpli_enabled_bridge=b;sink=s;sink_user=u;return 1;}
void vpli_decode_header_impl(uint32_t at,uint32_t word){if(bridge)(void)vpli_header(bridge,at,word);}
uint32_t vpli_decode_argument_impl(uint32_t at,uint32_t word){
 if(bridge){int ok=vpli_argument(bridge,at,word);if(method_observer){VpliPacket packet;method_observer(method_user,ok&&vpli_packet(bridge,&packet)?&packet:NULL,word);}}
 return word;
}
uint32_t vpli_read_header_impl(uint32_t at,VpliRawRead read){
 uint32_t word;if(bridge)(void)vpli_read_header(bridge,at,read,&word);else word=read(at);return word;
}
uint32_t vpli_read_argument_impl(uint32_t at,VpliRawRead read){
 uint32_t word;if(bridge){int ok=vpli_read_argument(bridge,at,read,&word);if(method_observer){VpliPacket packet;method_observer(method_user,ok&&vpli_packet(bridge,&packet)?&packet:NULL,word);}}else word=read(at);return word;
}
void vpli_program_source(const VfpSource*source){if(bridge){VpliPacket p;if(!source||!vpli_draw_packet(bridge,&p)||!sink(sink_user,&p,source)){/* Semantic/frame rejection never relabels or kills ordered byte tracking. */bridge->config.fault(bridge->config.user,VPL_ORDER);}}}
/* Host observations call these separately from original instructions. */
VpliBridge*vpli_hooks_bridge(void){return bridge;}

int vpli_ack_begin_hook(void){return bridge?vpli_ack_initial_begin(bridge):0;}
void vpli_ack_commit_hook(uint32_t p){if(bridge)(void)vpli_ack_initial_commit(bridge,p);}
