/* Original-RUI preferences model; guest thread only after configure. djultra64. */
#ifndef TOPSPIN_NATIVE_GRAPHICS_H
#define TOPSPIN_NATIVE_GRAPHICS_H
#include "graphics_settings.h"
typedef int (*TopspinNativeGraphicsReady)(const TopspinGraphicsSettings*,void*);
typedef struct {
 char path[4096],status[40];
 TopspinGraphicsSettings saved,requested,running;
 TopspinNativeGraphicsReady ready;void*user;
 int configured;
} TopspinNativeGraphics;
int topspin_native_graphics_configure(TopspinNativeGraphics*,const char*,TopspinNativeGraphicsReady,void*,unsigned,TopspinGraphicsAA,TopspinGraphicsFilter);
void topspin_native_graphics_begin(TopspinNativeGraphics*);
/* Rows 0 resolution,1 AA,2 sprite filter,3 frame limit,4 window mode. Never writes files. */
int topspin_native_graphics_adjust(TopspinNativeGraphics*,unsigned,int);
int topspin_native_graphics_value(const TopspinNativeGraphics*,unsigned,char*,size_t);
int topspin_native_graphics_save(TopspinNativeGraphics*);
#endif
