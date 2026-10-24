/* Opt-in CPU-only decoded-frame evidence. djultra64. */
#ifndef TOPSPIN_MENU_MOVIE_CAPTURE_H
#define TOPSPIN_MENU_MOVIE_CAPTURE_H
#include <stdint.h>
/* Guest thread: copy validated bytes only; never opens files. */
void topspin_menu_movie_capture_after(uint32_t node,uint32_t original_stack);
/* Host main thread, outside guest token: flush at most three ready slots. */
void topspin_menu_movie_capture_poll(void);
#endif
