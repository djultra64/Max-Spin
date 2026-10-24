/* Isolated requested graphics preferences. Own work: djultra64. */
#ifndef TOPSPIN_GRAPHICS_SETTINGS_H
#define TOPSPIN_GRAPHICS_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#define TOPSPIN_GRAPHICS_INI_LIMIT 4096u
typedef enum {TOPSPIN_AA_GUEST,TOPSPIN_AA_OFF,TOPSPIN_AA_MSAA2,TOPSPIN_AA_MSAA4,TOPSPIN_AA_MSAA8,TOPSPIN_AA_SSAA2X2,TOPSPIN_AA_COUNT} TopspinGraphicsAA;
typedef enum {TOPSPIN_FILTER_ORIGINAL,TOPSPIN_FILTER_SUPERXBR2X,TOPSPIN_FILTER_SUPERXBR4X,TOPSPIN_FILTER_COUNT} TopspinGraphicsFilter;
typedef enum {TOPSPIN_WINDOW_WINDOWED,TOPSPIN_WINDOW_FULLSCREEN,TOPSPIN_WINDOW_MODE_COUNT} TopspinWindowMode;
typedef struct {
 uint32_t version,resolution_height; /* Nominal16:9 output/base resolution; SSAA increases internal raster. */
 TopspinGraphicsAA aa;TopspinGraphicsFilter filter2d;
 uint32_t frame_cap; /* Upper bound only.0 removes an additional host cap. */
 TopspinWindowMode window_mode; /* Desktop fullscreen; saved/restart, same owner. */
} TopspinGraphicsSettings;
/* Resolution bits:720/900/1080/1440/1800/2160. AA/filter bits use their enums.
 * Cap bits:60/90/120/0. These are declared contracts, not GPU auto-detection. */
typedef struct {
 uint32_t resolutions,aa,filters,frame_caps;
 uint32_t aa_by_resolution[6]; /* Required pair mask; avoids advertising unsupported4K+MSAA combinations. */
} TopspinGraphicsCapabilities;
#ifdef __cplusplus
extern "C" {
#endif
void topspin_graphics_settings_defaults(TopspinGraphicsSettings*);
void topspin_graphics_capabilities_defaults(TopspinGraphicsCapabilities*);
uint32_t topspin_graphics_resolution_width(uint32_t height);
int topspin_graphics_settings_validate(const TopspinGraphicsSettings*,char*,size_t);
int topspin_graphics_settings_supported(const TopspinGraphicsSettings*,const TopspinGraphicsCapabilities*,char*,size_t);
/* Complete schema only. Parse/load leave output unchanged on any error. */
int topspin_graphics_settings_parse(const char*,size_t,TopspinGraphicsSettings*,char*,size_t);
int topspin_graphics_settings_format(const TopspinGraphicsSettings*,char*,size_t,char*,size_t);
/* Frozen startup preference shared by the owner and native menu model. */
TopspinWindowMode topspin_graphics_session_window_mode(void);
int topspin_graphics_settings_load(const char*path,TopspinGraphicsSettings*,char*,size_t);
/* Explicit caller-owned future graphics.ini; no chosen data root or mkdir.
 * Validates capability before IO, commits regular file by atomic rename,
 * then updates active. On failure both remain unchanged. Does not activate
 * a renderer, apply timing changes, or integrate any settings UI. */
int topspin_graphics_settings_apply(const char*path,const TopspinGraphicsSettings*requested,
 const TopspinGraphicsCapabilities*,TopspinGraphicsSettings*active,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
