/* Actual executor owner/callback TLS and token release on native threads.
 * Own work: djultra64. */
#include "guest_turn_windows.h"
#include "kernel/nv2a_executor_job.h"
#include "kernel/resource_epoch_api.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
int xbox_IrqlPreemptible(void){return 1;}
int xbox_Nv2aDrainSnapshot(XboxNv2aDrainState *state){(void)state;CHECK(0);return 0;}
static int echo(const void *input,uint32_t input_bytes,void *output,uint32_t output_bytes){
 CHECK(nv2a_executor_is_owner());CHECK(input_bytes==sizeof(int)&&output_bytes==sizeof(int));
 *(int*)output=*(const int*)input+1;
 int nested=0;CHECK(nv2a_executor_invoke(echo,input,input_bytes,&nested,sizeof nested,10,NULL)==NV2A_EXECUTOR_REENTRANT);
 return 17;
}
static DWORD WINAPI caller(void *unused){
 (void)unused;guest_turn_join();CHECK(!nv2a_executor_is_owner());int input=41,output=0,result=0;
 guest_turn_interrupts_disable();CHECK(nv2a_executor_invoke(echo,&input,sizeof input,&output,sizeof output,1000,&result)==NV2A_EXECUTOR_INVALID);guest_turn_interrupts_enable();
 CHECK(nv2a_executor_invoke(echo,&input,sizeof input,&output,sizeof output,1000,&result)==NV2A_EXECUTOR_DONE);
 CHECK(output==42&&result==17&&!nv2a_executor_is_owner());return 0;
}
int main(void){
 CHECK(!_putenv_s("RECOMP_EXECUTOR_INVOKE","1"));CHECK(nv2a_executor_jobs_start());CHECK(nv2a_executor_is_owner());
 HANDLE thread=CreateThread(NULL,0,caller,NULL,0,NULL);CHECK(thread);ULONGLONG deadline=GetTickCount64()+5000;int ran=0;
 while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT){ran+=nv2a_executor_jobs_service()==1;Sleep(0);CHECK(GetTickCount64()<deadline);}
 CHECK(ran==1);CHECK(nv2a_executor_is_owner());CloseHandle(thread);nv2a_executor_jobs_stop();CHECK(!nv2a_executor_is_owner());
 puts("PASS: actual executor owner TLS, CLI rejection, guest wait release, callback reentry and result delivery");return 0;
}
