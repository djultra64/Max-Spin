/* Targeted Environment movie geometry; own integration: djultra64. */
#ifndef TOPSPIN_MENU_MOVIE_LAYOUT_H
#define TOPSPIN_MENU_MOVIE_LAYOUT_H
#include <stdint.h>
typedef struct TopSpinMenuMovieBorrow {uint32_t matrix;int active;struct TopSpinMenuMovieBorrow *previous;} TopSpinMenuMovieBorrow;
/* Scope original movie draw; crop only the validated unit-quad UV payload.
 * Borrow that payload only for its draw call; restore before returning.
 * The original draw owns every CPU/register/render effect. */
int topspin_menu_movie_layout_begin(uint32_t node,uint32_t stack,TopSpinMenuMovieBorrow*);
void topspin_menu_movie_layout_end(TopSpinMenuMovieBorrow*);
/* Cross-TU originalCC030 draw returnCC1AD, original once. */
void __wrap_sub_001613A0(void);
#endif
