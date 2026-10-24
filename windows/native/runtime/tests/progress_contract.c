/* Real native epoch/notification and guest reacquisition tests. djultra64. */
#include "guest_turn_windows.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
uint64_t xbox_Nv2aProgressEpoch(void);
void xbox_Nv2aProgressNotify(void);
int xbox_Nv2aWaitProgress(uint64_t,DWORD);
int xbox_IrqlPreemptible(void){return 1;}
static volatile LONG ready;
static uint64_t observed;
static DWORD WINAPI wait_thread(void *unused){(void)unused;guest_turn_join();InterlockedIncrement(&ready);CHECK(xbox_Nv2aWaitProgress(observed,5000)==1);xbox_Nv2aProgressNotify();return 0;}
int main(void){
    guest_turn_join();uint64_t epoch=xbox_Nv2aProgressEpoch();
    CHECK(xbox_Nv2aWaitProgress(epoch,0)==0);
    xbox_Nv2aProgressNotify();CHECK(xbox_Nv2aWaitProgress(epoch,0)==1);
    observed=xbox_Nv2aProgressEpoch();
    ULONGLONG began=GetTickCount64();CHECK(xbox_Nv2aWaitProgress(observed,30)==0);CHECK(GetTickCount64()-began>=20);
    guest_turn_interrupts_disable();CHECK(xbox_Nv2aWaitProgress(observed,1)==-1);guest_turn_interrupts_enable();
    HANDLE threads[2];for(unsigned i=0;i<2;i++){threads[i]=CreateThread(NULL,0,wait_thread,NULL,0,NULL);CHECK(threads[i]);}
    began=GetTickCount64();while(ready!=2){guest_turn_checkpoint();Sleep(0);CHECK(GetTickCount64()-began<5000);}
    /* Notifier owns guest token, wakes both, then republishes while they try
     * reacquisition. Holding predicate lock across return would deadlock. */
    xbox_Nv2aProgressNotify();xbox_Nv2aProgressNotify();
    CHECK(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    CHECK(xbox_Nv2aProgressEpoch()>=observed+4);
    for(unsigned i=0;i<2;i++)CloseHandle(threads[i]);
    puts("PASS: native epoch, zero poll, timeout, CLI exclusion, two waiters and reacquisition ordering");return 0;
}
