/* Original bodies remain authoritative. Own diagnostics: djultra64. */
#include "career_trace.h"
#include "material_save_session.h"
#include "playercreator_model_checkpoint.h"
extern void sub_0011A2D0_gen(void);
extern void sub_00148230_gen(void);
extern void sub_0011AE70_gen(void);
void sub_0011A2D0(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_SAVE_WRITE);
 sub_0011A2D0_gen();topspin_career_trace_end(token);
}
void sub_00148230(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_PACK_SERIALIZE);
 sub_00148230_gen();topspin_career_trace_end(token);
}
void sub_0011AE70(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_PACK_SCAN);
 sub_0011AE70_gen();topspin_career_trace_end(token);
}

/* Exact named Save and descriptor gates, including same-TU call routing. */
extern void sub_000629D0_gen(void);
extern void sub_000C6A30_gen(void);
extern void sub_000C66A0_gen(void);
void sub_000629D0(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_SAVE_CALLBACK);
 sub_000629D0_gen();topspin_career_trace_end(token);
}
void sub_000C6A30(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_SAVE_DISPATCH);
 topspin_creator_model_checkpoint_observe("before-dispatch-selection");
 sub_000C6A30_gen();topspin_career_trace_end(token);
}
void sub_000C66A0(void)
{
 unsigned token=topspin_career_trace_begin(TOPSPIN_CAREER_CREATOR,TOPSPIN_CAREER_SAVE_DESCRIPTOR);
 topspin_creator_model_checkpoint_observe("before-phase-start");
 if(!mc_save_before()){topspin_career_trace_end(token);return;}
 sub_000C66A0_gen();topspin_creator_model_checkpoint_observe("before-phase-end");mc_save_after();topspin_career_trace_end(token);
}
