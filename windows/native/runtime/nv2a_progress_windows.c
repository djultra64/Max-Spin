/* Native GPU publication epoch. Own work: djultra64. */
#define GUEST_TURN_NO_WRAPPERS
#include "guest_turn_windows.h"
extern int xbox_IrqlPreemptible(void);
static SRWLOCK progress_lock=SRWLOCK_INIT;
static CONDITION_VARIABLE progress_changed=CONDITION_VARIABLE_INIT;
static uint64_t progress_epoch;
uint64_t xbox_Nv2aProgressEpoch(void) {
    AcquireSRWLockShared(&progress_lock);uint64_t value=progress_epoch;
    ReleaseSRWLockShared(&progress_lock);return value;
}
void xbox_Nv2aProgressNotify(void) {
    AcquireSRWLockExclusive(&progress_lock);++progress_epoch;
    WakeAllConditionVariable(&progress_changed);ReleaseSRWLockExclusive(&progress_lock);
}
int xbox_Nv2aWaitProgress(uint64_t observed,DWORD timeout_ms) {
    if(!xbox_IrqlPreemptible() || !guest_turn_interrupts_enabled())return -1;
    /* Drop before taking the predicate lock. Recheck after taking it, so a
     * producer between leave and lock cannot be lost. Regain after unlocking. */
    int left=timeout_ms?guest_turn_leave():0;
    ULONGLONG deadline=GetTickCount64()+timeout_ms;
    AcquireSRWLockExclusive(&progress_lock);
    while(progress_epoch==observed && timeout_ms) {
        DWORD remaining=INFINITE;
        if(timeout_ms!=INFINITE){ULONGLONG now=GetTickCount64();if(now>=deadline)break;remaining=(DWORD)(deadline-now);}
        if(!SleepConditionVariableSRW(&progress_changed,&progress_lock,remaining,0)) {
            if(GetLastError()==ERROR_TIMEOUT)break;
            ReleaseSRWLockExclusive(&progress_lock);guest_turn_return(left);return -2;
        }
    }
    int changed=progress_epoch!=observed;
    ReleaseSRWLockExclusive(&progress_lock);guest_turn_return(left);return changed;
}
