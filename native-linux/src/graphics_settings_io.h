/* Internal platform IO seam. Own work: djultra64. */
#ifndef TOPSPIN_GRAPHICS_SETTINGS_IO_H
#define TOPSPIN_GRAPHICS_SETTINGS_IO_H
#include <stddef.h>
/* A platform adapter provides this atomic-replace operation and public load.
 * Return0 must leave the destination unchanged. No renderer activation here. */
int topspin_graphics_settings_store_file(const char*path,const char*canonical_ini,char*error,size_t error_bytes);
#endif
