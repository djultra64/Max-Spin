/* Native Windows cooperative single guest CPU. Own work: djultra64.
 * Guest code checkpoints at generated labels/backedges and kernel returns.
 * Registered guest threads park cooperatively; never SuspendThread while they
 * own host runtime state. Eligible queued guests retain FIFO/urgent ordering. */
#define GUEST_TURN_NO_WRAPPERS
#include "guest_turn_windows.h"
#include "guest_thread_windows.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#if defined(_MSC_VER)
#define GT_TLS __declspec(thread)
#else
#define GT_TLS __thread
#endif
typedef struct Turn {
    int holding,queued,yielding;
    DWORD tid;
    HANDLE native;
    FILETIME created;
    unsigned suspend_count;
    LPTHREAD_START_ROUTINE start;
    LPVOID parameter;
    struct Turn *next,*queue_next;
    const volatile uint32_t *caller,*ring,*index;
    const void *entry;
    uint32_t context;
} Turn;
static SRWLOCK turn_lock=SRWLOCK_INIT;
static CONDITION_VARIABLE turn_changed=CONDITION_VARIABLE_INIT;
static INIT_ONCE turn_once=INIT_ONCE_STATIC_INIT;
static DWORD turn_fls=FLS_OUT_OF_INDEXES;
static Turn *holder,*all_threads,*queue_head,*queue_tail;
static uint64_t handoffs,kicks;
static int prioritize_urgent;
static ULONGLONG longest_wait;
static uintptr_t code_start,code_end;
static GT_TLS Turn *self;
static GT_TLS unsigned interrupts_disabled,critical_depth;
static Turn *eligible(void) {
    Turn *first=NULL;
    for(Turn *t=queue_head;t;t=t->queue_next)if(!t->suspend_count){
        if(!first)first=t;
        /* Query at selection time: SetThreadPriority may occur after birth
         * or while queued, including the kernel's timer/audio thread. */
        if(prioritize_urgent&&!t->yielding&&GetThreadPriority(t->native)==THREAD_PRIORITY_TIME_CRITICAL)return t;
    }
    return first;
}
static void unlink_queue(Turn *t) {
    Turn **link=&queue_head,*previous=NULL;
    while(*link&&*link!=t){previous=*link;link=&(*link)->queue_next;}
    if(!*link)abort();*link=t->queue_next;if(queue_tail==t)queue_tail=previous;
    t->queue_next=NULL;t->queued=0;
}
static void release_turn(Turn *t) {
    AcquireSRWLockExclusive(&turn_lock);
    if(holder!=t||!t->holding)abort();
    t->holding=0;holder=NULL;WakeAllConditionVariable(&turn_changed);
    ReleaseSRWLockExclusive(&turn_lock);
}
static void acquire_turn(Turn *t,int yielding) {
    ULONGLONG began=GetTickCount64();
    AcquireSRWLockExclusive(&turn_lock);
    if(t->queued)abort();t->queued=1;t->yielding=yielding;t->queue_next=NULL;
    if(queue_tail)queue_tail->queue_next=t;else queue_head=t;queue_tail=t;
    while(holder||t->suspend_count||eligible()!=t){
        kicks++;if(!SleepConditionVariableSRW(&turn_changed,&turn_lock,INFINITE,0))abort();
    }
    unlink_queue(t);holder=t;t->holding=1;
    ULONGLONG elapsed=GetTickCount64()-began;if(elapsed>longest_wait)longest_wait=elapsed;
    ReleaseSRWLockExclusive(&turn_lock);
}
static VOID CALLBACK thread_gone(PVOID pointer) {
    Turn *t=pointer;
    if(t->holding)release_turn(t);
    AcquireSRWLockExclusive(&turn_lock);
    if(t->queued)unlink_queue(t);
    Turn **link=&all_threads;while(*link&&*link!=t)link=&(*link)->next;
    if(*link)*link=t->next;WakeAllConditionVariable(&turn_changed);
    ReleaseSRWLockExclusive(&turn_lock);
    if(self==t)self=NULL;if(t->native)CloseHandle(t->native);free(t);
}
static BOOL CALLBACK initialize_turn(PINIT_ONCE once,PVOID parameter,PVOID *context) {
    (void)once;(void)parameter;(void)context;
    const char *priority=getenv("RECOMP_GUEST_TURN_PRIO");prioritize_urgent=!(priority&&*priority=='0');
    turn_fls=FlsAlloc(thread_gone);return turn_fls!=FLS_OUT_OF_INDEXES;
}
static int identify(Turn *t,HANDLE thread) {
    FILETIME exited,kernel,user;
    t->tid=GetThreadId(thread);
    return t->tid&&GetThreadTimes(thread,&t->created,&exited,&kernel,&user)&&
        DuplicateHandle(GetCurrentProcess(),thread,GetCurrentProcess(),&t->native,0,FALSE,DUPLICATE_SAME_ACCESS);
}
static void register_turn(Turn *t) {
    AcquireSRWLockExclusive(&turn_lock);t->next=all_threads;all_threads=t;ReleaseSRWLockExclusive(&turn_lock);
}
void guest_turn_join(void) {
    if(self)return;
    if(!InitOnceExecuteOnce(&turn_once,initialize_turn,NULL,NULL))abort();
    Turn *t=calloc(1,sizeof *t);if(!t)abort();
    if(!identify(t,GetCurrentThread())){free(t);abort();}
    if(!FlsSetValue(turn_fls,t)){CloseHandle(t->native);free(t);abort();}
    self=t;register_turn(t);acquire_turn(t,0);
}
static DWORD WINAPI guest_thread_start(LPVOID parameter) {
    Turn *t=parameter;self=t;
    if(!FlsSetValue(turn_fls,t))abort();
    acquire_turn(t,0);return t->start(t->parameter);
}
HANDLE guest_turn_CreateGuestThread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack_bytes,LPTHREAD_START_ROUTINE start,LPVOID parameter,DWORD flags,LPDWORD thread_id) {
    if(!start){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    if(!InitOnceExecuteOnce(&turn_once,initialize_turn,NULL,NULL))return NULL;
    Turn *t=calloc(1,sizeof *t);if(!t){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    t->start=start;t->parameter=parameter;t->suspend_count=(flags&CREATE_SUSPENDED)?1:0;
    /* Register before releasing the initial native start gate. Every guest
     * handle can then be suspended safely even before its first guest turn. */
    HANDLE thread=CreateThread(attributes,stack_bytes,guest_thread_start,t,flags|CREATE_SUSPENDED,thread_id);
    if(!thread){free(t);return NULL;}
    if(!identify(t,thread)){
        DWORD error=GetLastError();TerminateThread(thread,error);CloseHandle(thread);free(t);SetLastError(error);return NULL;
    }
    register_turn(t);
    if(ResumeThread(thread)==(DWORD)-1){
        DWORD error=GetLastError();TerminateThread(thread,error);thread_gone(t);CloseHandle(thread);SetLastError(error);return NULL;
    }
    return thread;
}
static Turn *find_turn(DWORD tid,const FILETIME *created) {
    for(Turn *t=all_threads;t;t=t->next)if(t->tid==tid&&!CompareFileTime(&t->created,created))return t;
    return NULL;
}
static int thread_identity(HANDLE thread,DWORD *tid,FILETIME *created) {
    FILETIME exited,kernel,user;*tid=GetThreadId(thread);
    return *tid&&GetThreadTimes(thread,created,&exited,&kernel,&user);
}
DWORD guest_turn_SuspendGuestThread(HANDLE thread) {
    DWORD tid;FILETIME created;if(!thread_identity(thread,&tid,&created))return (DWORD)-1;
    AcquireSRWLockExclusive(&turn_lock);Turn *t=find_turn(tid,&created);
    if(!t){ReleaseSRWLockExclusive(&turn_lock);return SuspendThread(thread);}
    if(t->suspend_count==127||(t==self&&(interrupts_disabled||critical_depth))){ReleaseSRWLockExclusive(&turn_lock);SetLastError(ERROR_INVALID_PARAMETER);return (DWORD)-1;}
    DWORD previous=t->suspend_count++;int park=t==self&&t->holding;
    WakeAllConditionVariable(&turn_changed);ReleaseSRWLockExclusive(&turn_lock);
    if(park)guest_turn_checkpoint();return previous;
}
DWORD guest_turn_ResumeGuestThread(HANDLE thread) {
    DWORD tid;FILETIME created;if(!thread_identity(thread,&tid,&created))return (DWORD)-1;
    AcquireSRWLockExclusive(&turn_lock);Turn *t=find_turn(tid,&created);
    if(!t){ReleaseSRWLockExclusive(&turn_lock);return ResumeThread(thread);}
    DWORD previous=t->suspend_count;if(previous)t->suspend_count--;
    WakeAllConditionVariable(&turn_changed);ReleaseSRWLockExclusive(&turn_lock);return previous;
}
void guest_turn_set_code(const void *start,size_t size) {
    AcquireSRWLockExclusive(&turn_lock);code_start=(uintptr_t)start;
    code_end=size>UINTPTR_MAX-code_start?UINTPTR_MAX:code_start+size;ReleaseSRWLockExclusive(&turn_lock);
}
void guest_turn_checkpoint(void) {
    if(!self||!self->holding||interrupts_disabled||critical_depth)return;
    AcquireSRWLockExclusive(&turn_lock);int waiting=self->suspend_count||eligible()!=NULL;
    if(waiting)handoffs++;ReleaseSRWLockExclusive(&turn_lock);
    if(waiting){release_turn(self);acquire_turn(self,1);}
}
void guest_turn_interrupts_disable(void){interrupts_disabled=1;}
void guest_turn_interrupts_enable(void){interrupts_disabled=0;}
int guest_turn_interrupts_enabled(void){return !interrupts_disabled;}
int guest_turn_leave(void){if(!self||!self->holding||interrupts_disabled||critical_depth)return 0;release_turn(self);return 1;}
void guest_turn_return(int left){if(left){if(!self||self->holding)abort();acquire_turn(self,0);}}
void guest_turn_counts(uint64_t *a,uint64_t *b){AcquireSRWLockShared(&turn_lock);if(a)*a=handoffs;if(b)*b=kicks;ReleaseSRWLockShared(&turn_lock);}
long guest_turn_longest_wait_ms(void){AcquireSRWLockExclusive(&turn_lock);long result=longest_wait>LONG_MAX?LONG_MAX:(long)longest_wait;longest_wait=0;ReleaseSRWLockExclusive(&turn_lock);return result;}
void guest_turn_note_caller(const volatile uint32_t *p){if(self)self->caller=p;}
void guest_turn_note_start(const void *p,uint32_t c){if(self){self->entry=p;self->context=c;}}
void guest_turn_note_icall_ring(const volatile uint32_t *r,const volatile uint32_t *i){if(self){self->ring=r;self->index=i;}}
void guest_turn_note_extra(const char *key,int (*fn)(char *,size_t)){(void)key;(void)fn;}
int guest_turn_probe(char *out,size_t size,char *all,size_t all_size) {
    AcquireSRWLockShared(&turn_lock);int held=holder!=NULL;unsigned waiting=0,suspended=0;
    for(Turn *t=all_threads;t;t=t->next){waiting+=t->queued;suspended+=t->suspend_count!=0;}
    if(out&&size)snprintf(out,size,"{\"thread\":%lu,\"waiting\":%u,\"suspended\":%u,\"guest_lo\":%llu,\"guest_hi\":%llu}",holder?holder->tid:0,waiting,suspended,(unsigned long long)code_start,(unsigned long long)code_end);
    if(all&&all_size)snprintf(all,all_size,"[]");ReleaseSRWLockShared(&turn_lock);return held;
}
static DWORD return_wait(int left,DWORD result) {DWORD error=GetLastError();guest_turn_return(left);SetLastError(error);return result;}
DWORD guest_turn_WaitForSingleObject(HANDLE h,DWORD ms){int left=ms?guest_turn_leave():0;DWORD r=WaitForSingleObject(h,ms);return return_wait(left,r);}
DWORD guest_turn_WaitForSingleObjectEx(HANDLE h,DWORD ms,BOOL a){int left=ms?guest_turn_leave():0;DWORD r=WaitForSingleObjectEx(h,ms,a);return return_wait(left,r);}
DWORD guest_turn_WaitForMultipleObjects(DWORD n,const HANDLE *h,BOOL all,DWORD ms){int left=ms?guest_turn_leave():0;DWORD r=WaitForMultipleObjects(n,h,all,ms);return return_wait(left,r);}
DWORD guest_turn_WaitForMultipleObjectsEx(DWORD n,const HANDLE *h,BOOL all,DWORD ms,BOOL a){int left=ms?guest_turn_leave():0;DWORD r=WaitForMultipleObjectsEx(n,h,all,ms,a);return return_wait(left,r);}
DWORD guest_turn_SignalObjectAndWait(HANDLE signal,HANDLE h,DWORD ms,BOOL a){int left=ms?guest_turn_leave():0;DWORD r=SignalObjectAndWait(signal,h,ms,a);return return_wait(left,r);}
void guest_turn_Sleep(DWORD ms){if(!ms){guest_turn_checkpoint();Sleep(0);return;}int left=guest_turn_leave();Sleep(ms);guest_turn_return(left);}
DWORD guest_turn_SleepEx(DWORD ms,BOOL a){int left=guest_turn_leave();DWORD r=SleepEx(ms,a);return return_wait(left,r);}
BOOL guest_turn_SwitchToThread(void){guest_turn_checkpoint();return SwitchToThread();}
void guest_turn_EnterCriticalSection(LPCRITICAL_SECTION lock) {
    /* Never return holding a host mutex while waiting for the guest token.
     * A contended acquisition only waits for availability, releases it, then
     * reacquires the guest token and retries atomically with TryEnter. */
    while(!TryEnterCriticalSection(lock)) {
        int left=guest_turn_leave();
        EnterCriticalSection(lock);
        if(!left){critical_depth++;return;}
        LeaveCriticalSection(lock);
        guest_turn_return(left);
    }
    critical_depth++;
}
void guest_turn_LeaveCriticalSection(LPCRITICAL_SECTION lock){if(!critical_depth)abort();critical_depth--;LeaveCriticalSection(lock);}

BOOL guest_turn_TryEnterCriticalSection(LPCRITICAL_SECTION lock){BOOL result=TryEnterCriticalSection(lock);if(result)critical_depth++;return result;}
/* Guest synchronization objects can span guest code and blocking operations.
 * Unlike host runtime locks, owning one must not turn normal guest code into
 * an implicit CLI region. The native object still excludes its own waiters. */
BOOL guest_turn_GuestTryEnterCriticalSection(LPCRITICAL_SECTION lock){return TryEnterCriticalSection(lock);}
void guest_turn_GuestLeaveCriticalSection(LPCRITICAL_SECTION lock){LeaveCriticalSection(lock);}
void guest_turn_GuestEnterCriticalSection(LPCRITICAL_SECTION lock){
    while(!TryEnterCriticalSection(lock)){
        int left=guest_turn_leave();EnterCriticalSection(lock);
        if(!left)return;
        LeaveCriticalSection(lock);guest_turn_return(left);
    }
}
