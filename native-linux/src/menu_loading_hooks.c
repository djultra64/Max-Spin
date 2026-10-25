/* Exact Menu original loading-image route. Own work: djultra64. */
#define LOADING_LABEL "MENU"
#define LOADING_DEVICE 0x166058u
#define LOADING_CALLER 0xa4c0au
#define LOADING_LOCK_CALLER 0xcc4e5u
#define LOADING_DRAW_CALLER 0xcc62bu
#define LOADING_SELECTED_SIZE topspin_native_video_selected_size
#define LOADING_REAL_OUTER __real_sub_000CCE50
#define LOADING_REAL_LOCK __real_sub_0015B180
#define LOADING_REAL_DRAW __real_sub_00161650
#define LOADING_WRAP_OUTER __wrap_sub_000CCE50
#define LOADING_WRAP_LOCK __wrap_sub_0015B180
#define LOADING_WRAP_DRAW __wrap_sub_00161650
#include "loading_hook_impl.h"
