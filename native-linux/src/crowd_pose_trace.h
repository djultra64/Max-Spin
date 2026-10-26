/* Read-only source-pose flag observations. Own work: djultra64. */
#ifndef TOPSPIN_CROWD_POSE_TRACE_H
#define TOPSPIN_CROWD_POSE_TRACE_H
#ifndef TOPSPIN_CROWD_POSE_TRACE_LIMIT
#define TOPSPIN_CROWD_POSE_TRACE_LIMIT 512u
#endif
/* Exact TOPSPIN_CROWD_POSE_TRACE=1 opts in. stderr only; at most128 source
 * identities per thread and512 lines total. This observes a flag used by the
 * original billboard dimensions; it does not interpret the flag as a pose.
 * Route only the original definition to sub_000B7C70_crowd_trace_original. */
void sub_000B7C70(void);
#endif
