/* Generated from original full-body video profiles. Own work: djultra64.
 * Regenerate: video_framework_profiles.py --write-header */
#ifndef TOPSPIN_TITLE_VIDEO_PROFILES_H
#define TOPSPIN_TITLE_VIDEO_PROFILES_H
#include "native_video.h"
#if TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 1 /* menu */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "MENU",
    0x002075a0u,0x0032e6bcu,0x00166058u,0x0032e694u,
    0x0009cbb5u,0x0009b393u,0x000c8c3eu,0x003baa20u};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_00162700
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_00162700
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_00155A80
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_00155A80
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_000C98F0
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_000C98F0
#elif TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 2 /* boot */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "BOOT",
    0x0018c290u,0x0018fdc4u,0x000674d8u,0x001907c0u,
    0x00019695u,0x0002c723u,0x0001b5deu,0x001901d4u};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_00062FC0
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_00062FC0
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_00057740
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_00057740
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_0001C020
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_0001C020
#elif TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 3 /* career */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "CAREER",
    0x001f18b0u,0x002a3bdcu,0x00157638u,0x0037b8c4u,
    0x00031175u,0x000d8b93u,0x000e3dbeu,0x003bb500u};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_00153890
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_00153890
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_001472C0
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_001472C0
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_000E4A70
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_000E4A70
#elif TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 4 /* playercreator */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "PLAYERCREATOR",
    0x002b1660u,0x002b8b5cu,0x00168c68u,0x00390becu,
    0x000b8025u,0x000ba733u,0x00071aaeu,0x002b7898u};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_00164AD0
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_00164AD0
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_001588E0
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_001588E0
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_00072760
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_00072760
#elif TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 5 /* shop */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "SHOP",
    0x001ee200u,0x0029e980u,0x00153b08u,0x0039e5f8u,
    0x00019985u,0x000c95f3u,0x000de0ceu,0x003a4590u};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_0014F7B0
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_0014F7B0
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_00143780
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_00143780
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_000DED80
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_000DED80
#elif TOPSPIN_FRAMEWORK_VIDEO_PROFILE == 6 /* stop */
static const TopspinNativeVideoProfile topspin_framework_video_profile = {
    "STOP",
    0x0008eeb0u,0x0013df40u,0x000601c8u,0x00216f48u,
    0x00016725u,0x00018553u,0x0001d54eu,0x00215f4cu};
#define TOPSPIN_FRAMEWORK_REAL_CREATE __real_sub_0005BF50
#define TOPSPIN_FRAMEWORK_WRAP_CREATE __wrap_sub_0005BF50
#define TOPSPIN_FRAMEWORK_REAL_VIEWPORT __real_sub_00050A80
#define TOPSPIN_FRAMEWORK_WRAP_VIEWPORT __wrap_sub_00050A80
#define TOPSPIN_FRAMEWORK_REAL_CONSTRUCTOR __real_sub_0001DF90
#define TOPSPIN_FRAMEWORK_WRAP_CONSTRUCTOR __wrap_sub_0001DF90
#else
#error "Explicit Menu/Boot/Career/PlayerCreator/Shop/Stop profile required"
#endif
#endif
