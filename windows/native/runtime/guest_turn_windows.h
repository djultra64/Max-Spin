/* Native Windows virtual single guest CPU. Own work: djultra64. */
#ifndef MAXSPIN_GUEST_TURN_WINDOWS_H
#define MAXSPIN_GUEST_TURN_WINDOWS_H
#if !defined(_WIN32)
#error Native Windows runtime requires Windows
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void guest_turn_join(void);
void guest_turn_set_code(const void *start,size_t size);
void guest_turn_checkpoint(void);
void guest_turn_interrupts_disable(void);
void guest_turn_interrupts_enable(void);
int guest_turn_interrupts_enabled(void);
int guest_turn_leave(void);
void guest_turn_return(int left);
void guest_turn_counts(uint64_t *handoffs,uint64_t *kicks);
long guest_turn_longest_wait_ms(void);
void guest_turn_note_caller(const volatile uint32_t *slot);
void guest_turn_note_start(const void *entry,uint32_t context);
void guest_turn_note_icall_ring(const volatile uint32_t *ring,const volatile uint32_t *index);
void guest_turn_note_extra(const char *key,int (*fn)(char *,size_t));
int guest_turn_probe(char *holder,size_t holder_size,char *all,size_t all_size);
DWORD guest_turn_WaitForSingleObject(HANDLE object,DWORD milliseconds);
DWORD guest_turn_WaitForSingleObjectEx(HANDLE object,DWORD milliseconds,BOOL alertable);
DWORD guest_turn_WaitForMultipleObjects(DWORD count,const HANDLE *objects,BOOL all,DWORD milliseconds);
DWORD guest_turn_WaitForMultipleObjectsEx(DWORD count,const HANDLE *objects,BOOL all,DWORD milliseconds,BOOL alertable);
DWORD guest_turn_SignalObjectAndWait(HANDLE signal,HANDLE object,DWORD milliseconds,BOOL alertable);
void guest_turn_Sleep(DWORD milliseconds);
DWORD guest_turn_SleepEx(DWORD milliseconds,BOOL alertable);
BOOL guest_turn_SwitchToThread(void);
BOOL guest_turn_TryEnterCriticalSection(LPCRITICAL_SECTION lock);
void guest_turn_EnterCriticalSection(LPCRITICAL_SECTION lock);
BOOL guest_turn_GuestTryEnterCriticalSection(LPCRITICAL_SECTION lock);
void guest_turn_GuestEnterCriticalSection(LPCRITICAL_SECTION lock);
void guest_turn_GuestLeaveCriticalSection(LPCRITICAL_SECTION lock);
void guest_turn_LeaveCriticalSection(LPCRITICAL_SECTION lock);
#ifdef __cplusplus
}
#endif
/* This header must follow windows.h. Runtime implementation uses raw Win32.
 * CV waits deliberately have no blanket adapter: callers must drop the token
 * BEFORE acquiring their predicate lock and regain it AFTER releasing it. */
#ifndef GUEST_TURN_NO_WRAPPERS
#define WaitForSingleObject guest_turn_WaitForSingleObject
#define WaitForSingleObjectEx guest_turn_WaitForSingleObjectEx
#define WaitForMultipleObjects guest_turn_WaitForMultipleObjects
#define WaitForMultipleObjectsEx guest_turn_WaitForMultipleObjectsEx
#define SignalObjectAndWait guest_turn_SignalObjectAndWait
#define Sleep guest_turn_Sleep
#define SleepEx guest_turn_SleepEx
#define SwitchToThread guest_turn_SwitchToThread
#define TryEnterCriticalSection guest_turn_TryEnterCriticalSection
#define EnterCriticalSection guest_turn_EnterCriticalSection
#define LeaveCriticalSection guest_turn_LeaveCriticalSection
#endif
#endif
