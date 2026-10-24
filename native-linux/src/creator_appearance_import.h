/* Private appearance experiment only. Own work: djultra64. */
#ifndef TOPSPIN_CREATOR_APPEARANCE_IMPORT_H
#define TOPSPIN_CREATOR_APPEARANCE_IMPORT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Call before guest startup with the SAME saves root passed to this image.
 * Default OFF. Exact TOPSPIN_PRIVATE_APPEARANCE_IMPORT=1 additionally requires
 * a prepared, path-bound private marker and original decoded ADN reference.
 * Exact TOPSPIN_PRIVATE_APPEARANCE_DIAGNOSTIC=1 additionally selects bounded
 * Before/load/After/reload/Second output in private Cache; that mode never
 * exports CodeName/cache metadata even if canonical data matches.
 * This never validates or reconstructs a Career/signature/progress. */
int topspin_creator_appearance_configure(const char *saves_root);
#ifdef __cplusplus
}
#endif
#endif
