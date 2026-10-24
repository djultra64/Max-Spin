/* Original seams only. Own work: djultra64. */
#ifndef TOPSPIN_GAME_SCENE_LIFECYCLE_H
#define TOPSPIN_GAME_SCENE_LIFECYCLE_H
#include "scene_lifecycle.h"
typedef int(*TslSink)(void*,const TslSnapshot*);
/* Separate opt-ins: TOPSPIN_VISUAL_SCENE_LIFECYCLE=1 registers only;
 * TOPSPIN_VISUAL_SCENE_SNAPSHOT=1 additionally publishes at EB620 entry.
 * A missing real consumed-PB staging sink rejects snapshot startup. Sink must synchronously
 * copy this host object; it may not retain the stack pointer or re-enter guest.
 * Snapshot includes an exact atomic authority revision; the real GPU guard
 * must compare tsl_authority_revision() with the consumed interval revision before consuming.
 * Snapshot with version0 explicitly invalidates previously published authority.
 * The backend independently supplies its actual device epoch. */
int topspin_scene_lifecycle_configure(TslSink,void*);
/* Transport failure is latched. The real sink/backend must invalidate its own
 * recorder on failed publication; the hook cannot fabricate successful staging. */
int topspin_scene_lifecycle_failed(void);
#endif
