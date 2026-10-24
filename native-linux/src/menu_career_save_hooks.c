/* Original bodies remain authoritative. Own diagnostics: djultra64. */
#include "career_trace.h"
extern void sub_000F7280_gen(void);
extern void sub_000F5240_gen(void);
extern void sub_000F6400_gen(void);
void sub_000F7280(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_MENU,TOPSPIN_CAREER_VALIDATE);
 sub_000F7280_gen();topspin_career_trace_end(token);
}
void sub_000F5240(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_MENU,TOPSPIN_CAREER_PACK_LOAD);
 sub_000F5240_gen();topspin_career_trace_end(token);
}
void sub_000F6400(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_MENU,TOPSPIN_CAREER_SAVE_WRITE);
 sub_000F6400_gen();topspin_career_trace_end(token);
}
