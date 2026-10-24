/* Native original-RUI PC entries and host requests. Own work: djultra64. */
#ifndef TOPSPIN_MENU_PC_ENTRIES_H
#define TOPSPIN_MENU_PC_ENTRIES_H
#include <stdint.h>
#ifdef TOPSPIN_NATIVE_GRAPHICS
#include "native_graphics.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {TOPSPIN_MENU_PC_NONE=0,TOPSPIN_MENU_PC_OPEN_GRAPHICS=1,
              TOPSPIN_MENU_PC_QUIT=2} TopspinMenuPcRequest;
/* Main thread, before guest starts: load only private generated chunks.
 * Returns0 on malformed/missing/version-mismatched cache. No original writes. */
int topspin_menu_pc_configure(const char *private_directory);
#ifdef TOPSPIN_NATIVE_GRAPHICS
/* Before guest work; all subsequent page edits run with the guest token. */
int topspin_menu_graphics_configure(const char*,TopspinNativeGraphicsReady,void*,unsigned,TopspinGraphicsAA,TopspinGraphicsFilter);
#endif
/* Main thread only: consume once. Never accesses guest memory. */
TopspinMenuPcRequest topspin_menu_pc_take_host_request(void);
/* Main thread: after overlay closes or fails to open. Guest consumes later. */
void topspin_menu_pc_request_focus_restore(void);
/* Main thread: ACK after guest restore or discarded stale modal generation. */
int topspin_menu_pc_take_focus_restored(void);
/* Exact guest update boundary, with guest token held; preserves architectural
 * state and only restores if this generation remains in the PC modal state. */
void topspin_menu_pc_guest_poll(void);
/* Title hooks require original bodies renamed _gen by manual-hook routing. */
void sub_00023F30(void);
void sub_0002EF80(void);
void sub_0011D4D0(void);
void sub_00023780(void);
void sub_0002E7B0(void);
void sub_00023880(void);
void sub_0009CC50(void);
/* Optional passive original control-event trace; native Menu linkwrap only. */
#define TOPSPIN_MENU_PC_EVENT_TRACE 1
void __wrap_sub_000348E0(void);
#ifdef __cplusplus
}
#endif
#endif
