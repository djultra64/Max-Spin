/* Exact Game original loading-image route. Own work: djultra64. */
#define LOADING_LABEL "GAME"
#define LOADING_COURT_BACKDROPS 1
#define LOADING_DEVICE 0x1b9438u
#define LOADING_CALLER 0xe1adau
#define LOADING_LOCK_CALLER 0x75db5u
#define LOADING_DRAW_CALLER 0x75efbu
#define LOADING_SELECTED_SIZE topspin_game_video_selected_size
#define LOADING_REAL_OUTER __real_sub_00138340
#define LOADING_REAL_LOCK __real_sub_001AE760
#define LOADING_REAL_DRAW __real_sub_001B4C30
#define LOADING_WRAP_OUTER __wrap_sub_00138340
#define LOADING_WRAP_LOCK __wrap_sub_001AE760
#define LOADING_WRAP_DRAW __wrap_sub_001B4C30
#include "loading_hook_impl.h"
