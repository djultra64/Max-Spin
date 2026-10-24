/* Bounded passive original Menu input observations. djultra64. */
#ifndef TOPSPIN_MENU_INPUT_TRACE_H
#define TOPSPIN_MENU_INPUT_TRACE_H
#include <stddef.h>
#include <stdint.h>
#define TOPSPIN_MENU_INPUT_SAMPLES 128
#define TOPSPIN_MENU_INPUT_EDS 16
typedef struct {uint32_t va,word[4];} TopSpinMenuInputEd;
typedef struct {
 uint64_t ns;uint32_t phase,base,valid,manager,manager_flags[2];
 uint32_t port[4][8]; /* handle,inserted,removed, then18raw bytes+padding */
 uint32_t ohci[7]; /* control,status,enable,HCCA,currentED,controlHead,doneHead */
 uint32_t ed_count;TopSpinMenuInputEd ed[TOPSPIN_MENU_INPUT_EDS];
} TopSpinMenuInputSample;
typedef int (*TopSpinMenuInputRead)(void*,uint32_t,void*,size_t);
/* Pure bounded snapshot path; reader must reject inaccessible guest ranges.
 * MMIO reader is used only for the seven explicit non-clearing OHCI registers. */
int topspin_menu_input_snapshot(TopSpinMenuInputSample*,uint64_t,uint32_t,uint32_t,
                              TopSpinMenuInputRead,void*);
void topspin_menu_input_trace_poll(void); /* host thread, outside guest token */
void __wrap_sub_0009E4E0(void);
#endif
