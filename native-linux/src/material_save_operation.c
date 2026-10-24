/* Whole-operation transaction ownership, host TLS only. Own work: djultra64. */
#include "material_save_operation.h"
#include "material_save_transaction.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Configuration is immutable after startup; callers must configure before
 * creating workers, matching the existing transaction startup contract. */
static int configured,enabled;
static _Thread_local struct {
 unsigned state,kind,phases;
 uint32_t owner,caller;
 int failed,phase_open,transaction_open,result;
} operation;

int mc_save_operation_configure(void){
 if(configured)return 1;
 const char*value=getenv("TOPSPIN_CREATOR_MATERIAL_COHERENCE");
 enabled=value&&!strcmp(value,"1");configured=1;return 1;
}
int mc_save_operation_enter(unsigned kind,uint32_t owner,uint32_t caller){
 if(!configured)return 0;
 if(!enabled)return 1;
 if(operation.state){if(operation.state==1)operation.failed=1;return 0;}
 if((kind!=MC_SAVE_OPERATION_CREATOR&&kind!=MC_SAVE_OPERATION_SHOP)||
    owner<0x10000u||owner>0x03fffffcu||(owner&3u))return 0;
 memset(&operation,0,sizeof operation);operation.state=1;
 operation.kind=kind;operation.owner=owner;operation.caller=caller;return 1;
}
int mc_save_operation_active(void){return enabled&&operation.state==1;}
int mc_save_operation_can_continue(void){
 if(!configured)return 0;
 if(!enabled)return 1;
 return operation.state==1&&!operation.failed&&!operation.phase_open&&operation.phases>0;
}
int mc_save_operation_phase_begin(void){
 if(!configured)return 0;
 if(!enabled)return 1;
 if(operation.state!=1)return 0;
 if(operation.phase_open){operation.failed=1;return 0;}
 if(operation.failed)return 0;
 if(!operation.transaction_open){
  if(!mc_save_transaction_begin()){operation.failed=1;return 0;}
  operation.transaction_open=1;
 }
 operation.phase_open=1;return 1;
}
void mc_save_operation_phase_end(int complete){
 if(!enabled||operation.state!=1)return;
 if(!operation.phase_open){operation.failed=1;return;}
 operation.phase_open=0;
 if(!complete)operation.failed=1;
 if(operation.phases==UINT_MAX)operation.failed=1;
 else operation.phases++;
}
void mc_save_operation_reject(void){if(enabled&&operation.state==1)operation.failed=1;}
int mc_save_operation_finish(void){
 if(!configured)return 0;
 if(!enabled)return 1;
 if(operation.state==2)return operation.result;
 if(operation.state!=1)return 0;
 int complete=!operation.failed&&!operation.phase_open&&operation.phases>0;
 operation.result=0;
 /* Change state before backend IO: recursive calls cannot publish twice. */
 operation.state=2;
 if(operation.transaction_open){
  operation.transaction_open=0;
  int ended=mc_save_transaction_end(complete);
  operation.result=complete&&ended;
 }
 return operation.result;
}
void mc_save_operation_leave(void){
 if(!enabled)return;
 if(operation.transaction_open){
  operation.transaction_open=0;operation.state=2;operation.result=0;
  (void)mc_save_transaction_end(0);
 }
 memset(&operation,0,sizeof operation);
}
