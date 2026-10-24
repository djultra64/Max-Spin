/* Development input integration. Own work: djultra64. */
void topspin_controls_start(void);
void topspin_controls_key(unsigned key, int down);

#include "controller_actions.h"
/* Context is supplied by a verified title-state hook; no guessed state reads. */
void topspin_controls_context(TopSpinControlContext context);
int topspin_controls_prompt(TopSpinAction action,TopSpinActionPrompt *prompt);

/* Coherent applied physical sources for guest A/B/X/Y/Black/White. */
int topspin_controls_guest_sources(uint8_t sources[6]);

/* Applied six controls, physical right-axis tag, Back, Start: coherent snapshot. */
int topspin_controls_guest_sources9(uint8_t sources[9]);
