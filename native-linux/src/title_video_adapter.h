/* Tiny per-image LDwrap adapters; original once. Own work: djultra64. */
#include "title_video_profiles.h"
extern void TOPSPIN_FRAMEWORK_REAL_CREATE(void);
extern void TOPSPIN_FRAMEWORK_REAL_VIEWPORT(void);
extern void TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR(void);
void TOPSPIN_FRAMEWORK_WRAP_CREATE(void)
{topspin_native_video_create(&topspin_framework_video_profile,TOPSPIN_FRAMEWORK_REAL_CREATE);}
void TOPSPIN_FRAMEWORK_WRAP_VIEWPORT(void)
{topspin_native_video_viewport(&topspin_framework_video_profile,TOPSPIN_FRAMEWORK_REAL_VIEWPORT);}
void TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR(void)
{topspin_native_video_constructor(&topspin_framework_video_profile,TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR);}
