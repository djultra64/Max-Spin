/* Actual SDK import bodies + kernel suspend/resume implementations on native
 * registered guest threads. Own work: djultra64. */
#include <windows.h>
static DWORD (WINAPI *raw_wait)(HANDLE,DWORD)=WaitForSingleObject;
#include "guest_turn_windows.h"
#include "guest_thread_windows.h"
#include "kernel/kernel.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s winerr=%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
void xbox_log(int level,const char *category,const char *format,...){(void)level;(void)category;(void)format;}
#define xbox_SuspendGuestThread guest_turn_SuspendGuestThread
#define xbox_ResumeGuestThread guest_turn_ResumeGuestThread
#include "actual_thread_suspend.h"
static _Thread_local uint32_t g_eax,args[2];
static _Thread_local HANDLE resolved;
static _Thread_local PULONG previous_pointer;
#define STACK_ARG(index) args[index]
#define XBOX_TO_NATIVE(value) ((value)?(void*)previous_pointer:NULL)
static HANDLE bridge_resolve_handle(uint32_t token){CHECK(token==0x80000001u);return resolved;}
#include "actual_suspend_import.h"
static void imported(unsigned ordinal,HANDLE thread,PULONG previous){args[0]=0x80000001u;args[1]=previous?1:0;resolved=thread;previous_pointer=previous;g_eax=0xffffffffu;if(ordinal==231)bridge_NtSuspendThread();else bridge_NtResumeThread();CHECK(g_eax==STATUS_SUCCESS);}
static volatile LONG ran[8],release_region,after_region;
static HANDLE entered,go;
static CRITICAL_SECTION protected_lock;
static HANDLE self_handle;
static DWORD WINAPI mark(void *index){InterlockedIncrement(&ran[(uintptr_t)index]);return 0;}
static DWORD WINAPI protected_worker(void *unused){(void)unused;EnterCriticalSection(&protected_lock);SetEvent(entered);while(!release_region){guest_turn_checkpoint();Sleep(0);}LeaveCriticalSection(&protected_lock);guest_turn_checkpoint();InterlockedIncrement(&after_region);return 0;}
static DWORD WINAPI blocked_worker(void *unused){(void)unused;SetEvent(entered);CHECK(WaitForSingleObject(go,5000)==WAIT_OBJECT_0);InterlockedIncrement(&ran[6]);return 0;}
static DWORD WINAPI self_worker(void *unused){(void)unused;SetEvent(entered);ULONG previous=99;imported(231,self_handle,&previous);CHECK(previous==0);InterlockedIncrement(&ran[7]);return 0;}
int main(void){
 guest_turn_join();ULONG previous=99;InitializeCriticalSection(&protected_lock);entered=CreateEventW(NULL,TRUE,FALSE,NULL);go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(entered&&go);
 HANDLE b=guest_turn_CreateGuestThread(NULL,0,mark,(void*)1,0,NULL);HANDLE c=guest_turn_CreateGuestThread(NULL,0,mark,(void*)2,0,NULL);CHECK(b&&c);
 imported(231,b,&previous);CHECK(previous==0);imported(231,b,&previous);CHECK(previous==1);
 CHECK(WaitForSingleObject(c,5000)==WAIT_OBJECT_0);CHECK(ran[1]==0&&ran[2]==1);CloseHandle(c);
 imported(224,b,&previous);CHECK(previous==2);for(unsigned i=0;i<100;i++)guest_turn_checkpoint();CHECK(ran[1]==0);
 imported(224,b,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(b,5000)==WAIT_OBJECT_0&&ran[1]==1);CloseHandle(b);
 b=guest_turn_CreateGuestThread(NULL,0,mark,(void*)3,CREATE_SUSPENDED,NULL);CHECK(b);for(unsigned i=0;i<100;i++)guest_turn_checkpoint();CHECK(ran[3]==0);imported(224,b,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(b,5000)==WAIT_OBJECT_0&&ran[3]==1);CloseHandle(b);
 /* Request suspension while another host caller observes a guest holding a
  * runtime lock. It must finish that protected region before parking. */
 ResetEvent(entered);b=guest_turn_CreateGuestThread(NULL,0,protected_worker,NULL,0,NULL);CHECK(b);int left=guest_turn_leave();CHECK(left);CHECK(raw_wait(entered,5000)==WAIT_OBJECT_0);imported(231,b,&previous);CHECK(previous==0);
 c=guest_turn_CreateGuestThread(NULL,0,mark,(void*)4,0,NULL);CHECK(c);for(unsigned i=0;i<100;i++)Sleep(0);CHECK(ran[4]==0&&after_region==0);
 InterlockedExchange(&release_region,1);guest_turn_return(left);CHECK(after_region==0);CHECK(WaitForSingleObject(c,5000)==WAIT_OBJECT_0&&ran[4]==1);CloseHandle(c);imported(224,b,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(b,5000)==WAIT_OBJECT_0&&after_region==1);CloseHandle(b);
 /* A guest waking from a native wait must also honor pending suspension
  * before it can run more guest instructions. */
 ResetEvent(entered);b=guest_turn_CreateGuestThread(NULL,0,blocked_worker,NULL,0,NULL);CHECK(b);CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);imported(231,b,&previous);CHECK(previous==0);SetEvent(go);for(unsigned i=0;i<100;i++){guest_turn_checkpoint();Sleep(0);}CHECK(ran[6]==0);imported(224,b,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(b,5000)==WAIT_OBJECT_0&&ran[6]==1);CloseHandle(b);
 ResetEvent(entered);self_handle=guest_turn_CreateGuestThread(NULL,0,self_worker,NULL,0,NULL);CHECK(self_handle);CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);CHECK(ran[7]==0);imported(224,self_handle,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(self_handle,5000)==WAIT_OBJECT_0&&ran[7]==1);CloseHandle(self_handle);
 /* Never-registered host threads retain native Win32 suspend counts. */
 b=CreateThread(NULL,0,mark,(void*)5,CREATE_SUSPENDED,NULL);CHECK(b);imported(231,b,&previous);CHECK(previous==1);imported(224,b,&previous);CHECK(previous==2);CHECK(ran[5]==0);imported(224,b,&previous);CHECK(previous==1);CHECK(WaitForSingleObject(b,5000)==WAIT_OBJECT_0&&ran[5]==1);CloseHandle(b);
 puts("PASS: actual NtSuspend/NtResume imports skip suspended guests, nest counts, park safely, resume waits/self, preserve suspended birth and native host threads");return 0;
}
