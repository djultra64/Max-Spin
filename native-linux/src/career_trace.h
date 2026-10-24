/* Bounded, read-only original save/load observation. Own work: djultra64. */
#ifndef TOPSPIN_CAREER_TRACE_H
#define TOPSPIN_CAREER_TRACE_H
#include <stdint.h>
enum {TOPSPIN_CAREER_MENU=1,TOPSPIN_CAREER_CREATOR=2};
enum {TOPSPIN_CAREER_VALIDATE=1,TOPSPIN_CAREER_PACK_LOAD=2,
      TOPSPIN_CAREER_SAVE_WRITE=3,TOPSPIN_CAREER_PACK_SERIALIZE=4,
      TOPSPIN_CAREER_PACK_SCAN=5,TOPSPIN_CAREER_SAVE_CALLBACK=6,
      TOPSPIN_CAREER_SAVE_DISPATCH=7,TOPSPIN_CAREER_SAVE_DESCRIPTOR=8};
/* Host thread before guest, then poll only from host. No file output on guest. */
int topspin_career_trace_init(void);
void topspin_career_trace_poll(void);
/* Exact guest boundary, with guest execution ownership. Token0 is disabled. */
unsigned topspin_career_trace_begin(unsigned image,unsigned stage);
void topspin_career_trace_end(unsigned token);
#endif
