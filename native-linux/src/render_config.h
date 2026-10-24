/* Native render preferences. Own work: djultra64. */
#ifndef TOPSPIN_RENDER_CONFIG_H
#define TOPSPIN_RENDER_CONFIG_H
#include "graphics_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Before any device: supervisor TOPSPIN_SESSION_INTERNAL_HEIGHT pins one
 * persistent-window session; fresh owner query must remove inherited pin.
 * TOPSPIN_SESSION_RENDER_INI pins the entire raw preference per supervisor.
 * SSAA2x2 uses2x raster dimensions at720/900/1080; output remains nominal.
 * Otherwise explicit TOPSPIN_INTERNAL_HEIGHT720..2160 wins over
 * TOPSPIN_GRAPHICS_INI, then720default. Invalid INI safely falls back720 without
 * overwriting it; invalid explicit env fails. Guest video/projection stay720p.
 * Legacy TOPSPIN_RENDER_SCALE1..4 only when neither new source is selected. */
int topspin_configure_render_scale(void);
/* Requested real internal/output buffer size after successful configuration.
 * Parent persistent-window owner must resize output to match, separately. */
TopspinGraphicsAA topspin_render_antialiasing(void);
void topspin_render_output_size(unsigned *width,unsigned *height);
/* Read-only capability check, no GPU calls; usable as UI readiness callback.
 * Guest or SSAA2x2/original2D/60 preference; actual device limits plus
 * AA1 backing width checked, not a scene-memory/performance guarantee. */
int topspin_render_graphics_ready(const TopspinGraphicsSettings*,void*);
#ifdef __cplusplus
}
#endif
#endif
