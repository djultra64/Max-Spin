/* Original60Hz deadline accuracy; own work: djultra64. */
#ifndef TOPSPIN_GAME_FRAME_WAIT_H
#define TOPSPIN_GAME_FRAME_WAIT_H
#include <stdlib.h>
#include <string.h>
static inline int topspin_configure_game_frame_wait(const char *image)
{
    /* The SDK keeps the original absolute VBlank/PTIMER schedule and1ms
     * service ceiling, shortening only the last wait before a due VBlank.
     * Preserve explicit overrides and avoid changing other images. */
    if(!image||strcmp(image,"TopSpin(Game).xbe")||getenv("RECOMP_TIMER_DEADLINE_WAIT"))return 1;
    return setenv("RECOMP_TIMER_DEADLINE_WAIT","1",0)==0;
}
#endif
