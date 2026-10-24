/* External parent-owned X11 window contract. Own work: djultra64. */
#ifndef TOPSPIN_FOREIGN_WINDOW_H
#define TOPSPIN_FOREIGN_WINDOW_H
#include <stddef.h>
#include <stdint.h>
typedef struct {int enabled;uint32_t xid;} TopspinForeignWindow;
/* NULL xid disables. Empty/malformed supplied xid fails closed. SDL3's modern
 * SDL_VIDEO_DRIVER has precedence over its legacy SDL_VIDEODRIVER fallback. */
int topspin_foreign_window_parse(const char*xid,const char*legacy_driver,
 const char*modern_driver,TopspinForeignWindow*out,char*error,size_t error_bytes);
#endif
