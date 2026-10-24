/* Macro comes directly from the SDK source under test; no copied definition.
 * Own behavioral fixture: djultra64. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define GUEST_TURN_NO_WRAPPERS
#include "tls_under_test.h"
static TEST_TLS volatile uintptr_t slot;
static HANDLE written[2], proceed;
static volatile LONG failed;
static DWORD WINAPI worker(void *p) {
    unsigned index=(unsigned)(uintptr_t)p;
    if(slot!=0) InterlockedIncrement(&failed);
    slot=0x1234u+index;
    SetEvent(written[index]);
    WaitForSingleObject(proceed,5000);
    if(slot!=0x1234u+index) InterlockedIncrement(&failed);
    return 0;
}
int main(void) {
    HANDLE threads[2];
    proceed=CreateEventW(NULL,TRUE,FALSE,NULL);
    for(unsigned i=0;i<2;i++) written[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    threads[0]=CreateThread(NULL,0,worker,(void*)(uintptr_t)0,0,NULL);
    if(WaitForSingleObject(written[0],5000)!=WAIT_OBJECT_0) return 2;
    threads[1]=CreateThread(NULL,0,worker,(void*)(uintptr_t)1,0,NULL);
    if(WaitForSingleObject(written[1],5000)!=WAIT_OBJECT_0) return 2;
    if(slot!=0) InterlockedIncrement(&failed);
    SetEvent(proceed);
    if(WaitForMultipleObjects(2,threads,TRUE,5000)!=WAIT_OBJECT_0) return 2;
    if(failed) {fprintf(stderr,"FAIL: shared TLS state (%ld)\n",failed); return 1;}
    puts("PASS: actual SDK TLS qualifier isolates two native threads"); return 0;
}
