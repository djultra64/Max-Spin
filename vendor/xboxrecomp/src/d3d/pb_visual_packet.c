/* Own work djultra64; proposed, not a cadence or physics change. */
#include "pb_visual_packet.h"
#include <stdlib.h>
#include <string.h>
static PbVisualBackend backend;
static VfpBudget budget;
static VfpPacket*packet;
static PbVisualStatus status;
static PbVisualRequest request;
static VfpGeneration armed_generation;
static uint64_t nonce,before_serial;
static int configured,busy;
static void*allocate(void*u,size_t n){(void)u;return malloc(n);}
static void deallocate(void*u,void*p){(void)u;free(p);}
static int owned(void){return configured&&backend.owner(backend.user);}
static int same(VfpGeneration a,VfpGeneration b){return a.scene==b.scene&&a.view==b.view;}
int pb_visual_configure(const PbVisualBackend*b){
 if(configured||!b||!b->owner||!b->generation||!b->draw_serial||!b->capture||!b->release)return 0;
 backend=*b;budget=(VfpBudget){NULL,allocate,deallocate,VFP_MAX_PACKET_BYTES,0,1,0,0};configured=1;return 1;
}
int pb_visual_arm_job(const void*in,uint32_t ni,void*out,uint32_t no){
 PbVisualRequest r;VfpGeneration g;
 if(!owned()||busy||packet||status.state==1||!in||ni!=sizeof r||!out||no!=sizeof status)return -1;
 memcpy(&r,in,sizeof r);if(r.version!=1||!r.program_hash||!backend.generation(backend.user,&g)||!g.scene||!g.view)return -1;
 request=r;armed_generation=g;status=(PbVisualStatus){1,0,0};memcpy(out,&status,sizeof status);return 1;
}
int pb_visual_release_job(const void*in,uint32_t ni,void*out,uint32_t no){
 (void)in;if(!owned()||busy||ni||!out||no!=sizeof(int))return -1;
 if(packet&&!backend.release(backend.user,&packet))return -1;
 memset(&status,0,sizeof status);int result=1;memcpy(out,&result,sizeof result);return 1;
}
int pb_visual_interested(uint32_t hash){return status.state==1&&request.program_hash==hash;}
uint64_t pb_visual_before(uint32_t hash){
 VfpGeneration g;
 if(!pb_visual_interested(hash)||!owned()||busy||!backend.generation(backend.user,&g)||!same(g,armed_generation)||nonce==UINT64_MAX)return 0;
 before_serial=backend.draw_serial(backend.user);status.ticket=++nonce;return status.ticket;
}
int pb_visual_after(uint64_t ticket,const VfpSource*source){
 VfpGeneration g;VfpPacket*captured=NULL;
 if(!ticket||!owned()||busy||status.state!=1||ticket!=status.ticket)return 0;
 if(backend.draw_serial(backend.user)<=before_serial)return 0; /* cull/skip is not a producer */
 status.state=3;
 if(!source||source->program_hash!=request.program_hash||!backend.generation(backend.user,&g)||!same(g,armed_generation))return 0;
 VfpSource s=*source;s.generation=g;busy=1;
 int result=backend.capture(backend.user,&budget,&s,&captured);
 if(result==VFP_OK&&!captured)result=VFP_RESOURCE;
 if(result==VFP_OK&&(!backend.generation(backend.user,&g)||!same(g,armed_generation))){
  result=VFP_GENERATION;
 }
 status.result=(uint32_t)result;busy=0;
 if(result!=VFP_OK){if(captured&&!backend.release(backend.user,&captured))packet=captured;return 0;}
 packet=captured;status.state=2;return 1;
}
const VfpPacket*pb_visual_owned(void){return owned()&&status.state==2?packet:NULL;}
