/* Private authentic-old appearance equivalence proposal. Author: djultra64. */
#ifndef TOPSPIN_APPEARANCE_EQUIVALENCE_H
#define TOPSPIN_APPEARANCE_EQUIVALENCE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* For export authority both outputs must be from separately successful
 * original save/decode calls. A preflight may compare After to itself only
 * to reject a structurally changed source before reload; it never authorizes
 * export. Final comparison requires separate After/Second read successes,
 * with original load/reload AL1 verified by the caller. This pure comparison
 * confers no Career validity, metadata export authority, or IO success. */
int topspin_creator_appearance_equivalent(const void *reference,size_t reference_bytes,const void *after,size_t after_bytes,const void *second,size_t second_bytes);
#ifdef __cplusplus
}
#endif
#endif
