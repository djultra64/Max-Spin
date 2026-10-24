/* Original XDK resource-reader probe before Save writes. djultra64. */
#ifndef TOPSPIN_MATERIAL_ORIGINAL_PROBE_H
#define TOPSPIN_MATERIAL_ORIGINAL_PROBE_H
#include <stdint.h>
typedef struct {uint32_t bits,pitch,surface_header[6],resource,level;} McOriginalProbe;
/* Runs on guest CPU owner only, not executor93. Original GetSurfaceLevel,
 * original read LockRect and original Release exactly once, no Save callbacks.
 * Restores borrowed16KiB stack + CPU state; outputs only on full ABI success. */
int mc_original_texture_probe(uint32_t texture,McOriginalProbe*);
int mc_original_texture_level_probe(uint32_t texture,uint32_t level,McOriginalProbe*);
typedef int (*McOriginalProbeCapture)(void*,uint32_t,const McOriginalProbe*);
/* Capture runs while the original temporary mip Surface is still live. */
int mc_original_texture_level_capture(uint32_t texture,uint32_t level,McOriginalProbeCapture,void*,McOriginalProbe*);
#endif
