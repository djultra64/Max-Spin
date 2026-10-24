/* Guarded native video framework. Own work: djultra64. */
#ifndef TOPSPIN_NATIVE_VIDEO_H
#define TOPSPIN_NATIVE_VIDEO_H
#include <stdint.h>
typedef struct TopspinNativeVideoProfile {
    const char *label;
    uint32_t record8,app_global,xdk_device_global,automatic_info_global;
    uint32_t create_return,constructor_return,viewport_return,primary_camera_global;
} TopspinNativeVideoProfile;
void topspin_native_video_create(const TopspinNativeVideoProfile *,void (*original)(void));
void topspin_native_video_viewport(const TopspinNativeVideoProfile *,void (*original)(void));
void topspin_native_video_constructor(const TopspinNativeVideoProfile *,void (*original)(void));
int topspin_native_video_selected_size(const char *image_label,uint32_t dimensions[2]);
#endif
