/* PC graphics preferences overlay. Own work: djultra64. */
#ifndef TOPSPIN_PC_GRAPHICS_UI_H
#define TOPSPIN_PC_GRAPHICS_UI_H
#include "graphics_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
/* NULL readiness is read-only. Called under the UI mutex; must not reenter UI,
 * touch guest RAM or issue GPU calls. Path is explicit; no directory creation. */
typedef int (*TopspinGraphicsReady)(const TopspinGraphicsSettings*,void*);
int topspin_pc_graphics_configure(const char*path,TopspinGraphicsReady,void*user);
/* Call before opening, using render_config's successful startup size. Saving
 * only commits next-start preferences; this current size never changes live. */
int topspin_pc_graphics_set_running_resolution(unsigned height);
int topspin_pc_graphics_set_running_aa(TopspinGraphicsAA aa);
void topspin_pc_graphics_register(void);
int topspin_pc_graphics_open(void);
int topspin_pc_graphics_event(const void*sdl_event);
/* Consume once on the host/event thread, then enqueue guest focus restoration. */
int topspin_pc_graphics_close_requested(void);
/* Guest bridge ACK after restoring focus. Input stays captured until this ACK
 * AND physical close controls release. Keep polling close_requested each loop. */
void topspin_pc_graphics_focus_restored(void);
/* Only after renderer stopped and submissions drained, while device still alive.
 * Never call from an input event with a live renderer. OS process exit may own
 * final cleanup instead. Must unregister before device destruction/recreation. */
void topspin_pc_graphics_shutdown(void);
#ifdef TOPSPIN_PC_GRAPHICS_CPU_TEST
int topspin_pc_graphics_cpu_resolution(unsigned height,int save);
int topspin_pc_graphics_cpu_family(int requested_family); /* -99 queries; invalid returns -99. */
int topspin_pc_graphics_cpu_frame(float width,float height,float dt);
int topspin_pc_graphics_cpu_pending_events(void);
int topspin_pc_graphics_cpu_atlas_glyph(unsigned id,uint8_t *rgba,float uv[4]);
int topspin_pc_graphics_cpu_face_glyph(unsigned position);
int topspin_pc_graphics_cpu_draw_vertex(unsigned index,float xyuv[4],uint32_t *color);
const char*topspin_pc_graphics_cpu_prompt(int position);
#endif
#ifdef __cplusplus
}
#endif
#endif
