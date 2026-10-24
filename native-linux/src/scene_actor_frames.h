/* Actor-only owned HOST sidecars; no whole-scene admission. djultra64. */
#ifndef TOPSPIN_SCENE_ACTOR_FRAMES_H
#define TOPSPIN_SCENE_ACTOR_FRAMES_H
#include "scene_output_frames.h"
int topspin_scene_actor_init(int enabled); /* after real PB + TSL, before workers */
int topspin_scene_actor_output(VsoOutput*);
int topspin_scene_actor_adopt(VsoOutput*,VsjFrame**,uint64_t sample,uint64_t published,const void*policy,size_t bytes);
#endif
