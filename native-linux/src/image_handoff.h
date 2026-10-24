#ifndef TOPSPIN_IMAGE_HANDOFF_H
#define TOPSPIN_IMAGE_HANDOFF_H
/* Own work: djultra64. Configure before bridge initialization/guest threads.
 * Optional TOPSPIN_LAUNCH_PAGE_IN/OUT contain raw 4096-byte snapshots.
 * Returns 1 on success, 0 for invalid input or failed output preparation. */
int topspin_image_handoff_init(void);
#endif
