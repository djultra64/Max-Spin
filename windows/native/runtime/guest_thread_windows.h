/* Cooperative suspension for registered guest threads. Own work: djultra64. */
#ifndef MAXSPIN_GUEST_THREAD_WINDOWS_H
#define MAXSPIN_GUEST_THREAD_WINDOWS_H
#include <windows.h>
HANDLE guest_turn_CreateGuestThread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack_bytes,LPTHREAD_START_ROUTINE start,LPVOID parameter,DWORD flags,LPDWORD thread_id);
DWORD guest_turn_SuspendGuestThread(HANDLE thread);
DWORD guest_turn_ResumeGuestThread(HANDLE thread);
#endif
