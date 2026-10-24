/* Native Windows behavioral contracts. Own work: djultra64. */
#include "guest_turn_windows.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); ExitProcess(1); } } while (0)
static HANDLE entered, finished, release_worker;
static volatile LONG inside, overlap, turns, start_spin;
static CRITICAL_SECTION protected_lock;
static DWORD WINAPI contender(void *unused) {
    (void)unused;
    guest_turn_join();
    if (InterlockedIncrement(&inside)!=1) InterlockedIncrement(&overlap);
    InterlockedIncrement(&turns);
    InterlockedDecrement(&inside);
    SetEvent(entered);
    WaitForSingleObject(release_worker,5000);
    SetEvent(finished);
    return 0; /* FLS must release a joined thread's token on return. */
}
static DWORD WINAPI loop_worker(void *unused) {
    (void)unused; guest_turn_join();
    for (unsigned i=0;i<20000;i++) {
        if (InterlockedIncrement(&inside)!=1) InterlockedIncrement(&overlap);
        InterlockedIncrement(&turns);
        InterlockedDecrement(&inside);
        guest_turn_checkpoint();
    }
    return 0;
}
static DWORD WINAPI guest_lock_worker(void *unused) {
    (void)unused;guest_turn_join();SetEvent(entered);
    guest_turn_GuestEnterCriticalSection(&protected_lock);
    SetEvent(finished);guest_turn_GuestLeaveCriticalSection(&protected_lock);return 0;
}
static DWORD WINAPI lock_worker(void *unused) {
    (void)unused; guest_turn_join();
    EnterCriticalSection(&protected_lock);
    SetEvent(entered);
    LeaveCriticalSection(&protected_lock);
    return 0;
}
int main(void) {
    HANDLE worker;
    entered=CreateEventW(NULL,TRUE,FALSE,NULL); finished=CreateEventW(NULL,TRUE,FALSE,NULL);
    release_worker=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(entered && finished && release_worker);
    InitializeCriticalSection(&protected_lock);
    guest_turn_join();
    guest_turn_interrupts_disable(); guest_turn_interrupts_disable();
    CHECK(!guest_turn_interrupts_enabled());
    CHECK(guest_turn_leave()==0);
    InterlockedIncrement(&inside);
    worker=CreateThread(NULL,0,contender,NULL,0,NULL); CHECK(worker);
    for (unsigned i=0;i<1000;i++) { guest_turn_checkpoint(); Sleep(0); }
    CHECK(turns==0 && WaitForSingleObject(entered,0)==WAIT_TIMEOUT);
    InterlockedDecrement(&inside);
    guest_turn_interrupts_enable(); /* One STI enables after repeated CLI. */
    CHECK(guest_turn_interrupts_enabled());
    CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0); /* releases token */
    CHECK(turns==1 && overlap==0);
    SetEvent(release_worker);
    CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(finished,0)==WAIT_OBJECT_0);
    CloseHandle(worker);
    worker=CreateThread(NULL,0,loop_worker,NULL,0,NULL); CHECK(worker);
    for (unsigned i=0;i<20000;i++) {
        CHECK(InterlockedIncrement(&inside)==1);
        InterlockedIncrement(&turns); InterlockedDecrement(&inside);
        guest_turn_checkpoint();
    }
    CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CHECK(turns==40001 && overlap==0); CloseHandle(worker);
    ResetEvent(entered);
    EnterCriticalSection(&protected_lock);
    worker=CreateThread(NULL,0,lock_worker,NULL,0,NULL); CHECK(worker);
    for (unsigned i=0;i<1000;i++) guest_turn_checkpoint();
    CHECK(WaitForSingleObject(entered,0)==WAIT_TIMEOUT);
    LeaveCriticalSection(&protected_lock);
    CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CloseHandle(worker);
    CHECK(WaitForSingleObject(entered,0)==WAIT_OBJECT_0);
    CHECK(TryEnterCriticalSection(&protected_lock));
    CHECK(guest_turn_leave()==0);LeaveCriticalSection(&protected_lock);
    ResetEvent(entered);ResetEvent(finished);
    guest_turn_GuestEnterCriticalSection(&protected_lock);
    worker=CreateThread(NULL,0,guest_lock_worker,NULL,0,NULL);CHECK(worker);
    CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(finished,0)==WAIT_TIMEOUT);
    guest_turn_GuestLeaveCriticalSection(&protected_lock);
    CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    CHECK(WaitForSingleObject(finished,0)==WAIT_OBJECT_0);
    puts("PASS: native token exclusion, idempotent CLI/STI, release around waits, cooperative spin progress, critical protection, thread exit");
    return 0;
}
