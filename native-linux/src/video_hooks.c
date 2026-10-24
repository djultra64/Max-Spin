/* Select the original title's720p mode at its one constructor call site.
 * Own compatibility work: djultra64. Original selector/projection/allocation
 * remain generated; this changes only the requested mode argument. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern _Thread_local uint32_t g_esp, g_ecx, g_eax;
extern void __real_sub_0018338A(void);
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_00088450_gen(void);
static uint32_t selected_720_context,selected_720_device;

void sub_00088450(void)
{
    int applied=0;
    uint32_t context=g_ecx;
    const char *wide=getenv("RECOMP_VIDEO_WIDESCREEN");
    const char *mode=getenv("RECOMP_VIDEO_MODE");
    /* EE2DD is a five-byte CALL; EE2E2 is its exact real guest return VA.
     * Do not substitute other callers or a mode the caller selected itself. */
    if(wide && !strcmp(wide,"1") && mode && !strcmp(mode,"720p")
       && g_esp>=0x10000u && g_esp<=0x03fffff8u) {
        uint32_t caller,requested;
        uint8_t *stack=(uint8_t*)((uintptr_t)g_xbox_mem_offset+g_esp);
        memcpy(&caller,stack,4);memcpy(&requested,stack+4,4);
        if(caller==0x000EE2E2u && requested==2u) {
            const uint32_t original_720p_mode=8u;
            memcpy(stack+4,&original_720p_mode,4);
            applied=1;
        }
    }
    sub_00088450_gen();
    if(applied) {
        uint32_t device=0,width=0,height=0,aspect=0;
        selected_720_context=selected_720_device=0;
        if(context>=0x10000u && context<=0x03fffff4u) {
            memcpy(&width,(void*)((uintptr_t)g_xbox_mem_offset+context+4),4);
            memcpy(&height,(void*)((uintptr_t)g_xbox_mem_offset+context+8),4);
            memcpy(&device,(void*)((uintptr_t)g_xbox_mem_offset+0x30c148u),4);
            if(device>=0x10000u && device<=0x03fffe68u)
                memcpy(&aspect,(void*)((uintptr_t)g_xbox_mem_offset+device+0x194u),4);
            if(width==1280 && height==720 && aspect==0x3fe38e39u) {
                selected_720_context=context;selected_720_device=device;
            }
        }
    }
    /* Observe only after original allocation/projection setup, on its CPU
     * thread. No new hook or GPU-thread guest-memory reads are required. */
    if(applied) {
        static int reported;
        const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
        if(!reported && trace && !strcmp(trace,"1")) {
            uint32_t device,raw,width=0,height=0;
            if(context>=0x10000u && context<=0x03fffff4u) {
                memcpy(&width,(void*)((uintptr_t)g_xbox_mem_offset+context+4),4);
                memcpy(&height,(void*)((uintptr_t)g_xbox_mem_offset+context+8),4);
            }
            memcpy(&device,(void*)((uintptr_t)g_xbox_mem_offset+0x30c148u),4);
            reported=1;
            if(device>=0x10000u && device<=0x03fffe68u) {
                float aspect;
                memcpy(&raw,(void*)((uintptr_t)g_xbox_mem_offset+device+0x194u),4);
                memcpy(&aspect,&raw,4);
                fprintf(stderr,"[VIDEO-TITLE] requested_mode=8 selector_al=%u context=%08x selected_size=%ux%u device=%08x aspect_raw=%08x aspect=%.9g\n",
                        g_eax&255u,context,width,height,device,raw,(double)aspect);
            } else {
                fprintf(stderr,"[VIDEO-TITLE] requested_mode=8 selector_al=%u context=%08x selected_size=%ux%u device=%08x aspect_unavailable=invalid_device\n",
                        g_eax&255u,context,width,height,device);
            }
        }
    }
}

/* Link-time diagnostic for calls originating in other translation units.
 * --wrap=sub_0018338A redirects those references without regeneration; the
 * real title function still performs every capability query and restriction. */
void __wrap_sub_0018338A(void)
{
    uint32_t caller=0;
    if(g_esp>=0x10000u && g_esp<=0x03fffffcu)
        memcpy(&caller,(void*)((uintptr_t)g_xbox_mem_offset+g_esp),4);
    __real_sub_0018338A();
    static unsigned reported;
    const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
    if(trace && !strcmp(trace,"1") && reported<8) {
        ++reported;
        fprintf(stderr,"[VIDEO-FLAGS] caller=%08x returned=%08x\n",caller,g_eax);
    }
}

extern void __real_sub_001A8750(void);
void __wrap_sub_001A8750(void)
{
    uint32_t caller=0,parameters=0,pp[7]={0};
    int valid=0;
    const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
    int enabled=trace && !strcmp(trace,"1");
    if(enabled && g_esp>=0x10000u && g_esp<=0x03fffff8u) {
        uint8_t *stack=(uint8_t*)((uintptr_t)g_xbox_mem_offset+g_esp);
        memcpy(&caller,stack,4);memcpy(&parameters,stack+4,4);
        if(parameters>=0x10000u && parameters<=0x03ffffe4u) {
            memcpy(pp,(void*)((uintptr_t)g_xbox_mem_offset+parameters),sizeof pp);
            valid=1;
        }
    }
    __real_sub_001A8750();
    static unsigned reported;
    if(enabled && reported<4) {
        ++reported;
        fprintf(stderr,"[VIDEO-RESET] caller=%08x pp=%08x valid=%d width=%u height=%u format=%08x count=%u raw_10_14_18=%08x/%08x/%08x hresult=%08x\n",
                caller,parameters,valid,pp[0],pp[1],pp[2],pp[3],pp[4],pp[5],pp[6],g_eax);
    }
}


/* Original callers push a D3DVIEWPORT8 pointer (six DWORDs), then CALL.
 * Observe input before the real function consumes its stdcall argument.
 * Remember unique callsite/viewport tuples so alternating shadow/main calls
 * cannot exhaust the bounded diagnostic before a later scene transition.
 * Reserve the last16 entries for6D150 or main-sized viewport observations. */
extern void __real_sub_001A8F60(void);
void __wrap_sub_001A8F60(void)
{
    uint32_t caller=0,parent=0,pointer=0,viewport[6]={0};
    int valid=0;
    const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
    int enabled=trace && !strcmp(trace,"1");
    if(enabled && g_esp>=0x10000u && g_esp<=0x03fffff8u) {
        const uint8_t *stack=(const uint8_t*)((uintptr_t)g_xbox_mem_offset+g_esp);
        memcpy(&caller,stack,4);memcpy(&pointer,stack+4,4);
        /* 6D150 has no local stack allocation: its caller is directly above
         * the viewport argument after its CALL into this XDK function. */
        if(caller==0x0006D193u && g_esp<=0x03fffff4u)
            memcpy(&parent,stack+8,4);
        /* 128040 reserves24 local bytes, pushes its viewport pointer, then
         * CALLs1A8F60. Its own caller return remains at entryESP+32. */
        if(caller==0x00128083u && g_esp<=0x03ffffdcu)
            memcpy(&parent,stack+32,4);
        if(pointer>=0x10000u && pointer<=0x03ffffe8u) {
            memcpy(viewport,(const void*)((uintptr_t)g_xbox_mem_offset+pointer),sizeof viewport);
            valid=1;
        }
    }
    __real_sub_001A8F60();
    if(enabled) {
        struct observed {uint32_t caller,parent,viewport[6];int valid;};
        static struct observed seen[128];
        static unsigned reported;
        int critical=caller==0x0006D193u || (valid && viewport[2]>=640u && viewport[3]>=480u);
        if(reported>=128 || (!critical && reported>=112))return;
        for(unsigned i=0;i<reported;i++)
            if(seen[i].caller==caller && seen[i].parent==parent && seen[i].valid==valid
               && !memcmp(seen[i].viewport,viewport,sizeof viewport))return;
        struct observed *entry=&seen[reported++];
        entry->caller=caller;entry->parent=parent;entry->valid=valid;
        memcpy(entry->viewport,viewport,sizeof viewport);
        fprintf(stderr,"[VIDEO-VIEWPORT] caller=%08x parent=%08x ptr=%08x valid=%d rect=%u,%u,%u,%u z_bits=%08x/%08x\n",
            caller,parent,pointer,valid,viewport[0],viewport[1],viewport[2],viewport[3],viewport[4],viewport[5]);
    }
}


/* F079D restores a literal640x480 after the title's temporary viewports.
 * For the confirmed original720p mode only, restore its selected dimensions.
 * Every other setter call executes with its original arguments. */
extern void __real_sub_0006D150(void);
void __wrap_sub_0006D150(void)
{
    const char *wide=getenv("RECOMP_VIDEO_WIDESCREEN");
    const char *mode=getenv("RECOMP_VIDEO_MODE");
    if(selected_720_context && g_ecx==selected_720_device && wide && !strcmp(wide,"1")
       && mode && !strcmp(mode,"720p") && g_esp>=0x10000u && g_esp<=0x03ffffecu) {
        uint32_t args[5];
        uint8_t *stack=(uint8_t*)((uintptr_t)g_xbox_mem_offset+g_esp);
        memcpy(args,stack,sizeof args);
        if(args[0]==0x000F07A2u && args[1]==0 && args[2]==0 && args[3]==640 && args[4]==480) {
            uint32_t width,height,aspect,device;
            memcpy(&width,(void*)((uintptr_t)g_xbox_mem_offset+selected_720_context+4),4);
            memcpy(&height,(void*)((uintptr_t)g_xbox_mem_offset+selected_720_context+8),4);
            memcpy(&aspect,(void*)((uintptr_t)g_xbox_mem_offset+selected_720_device+0x194u),4);
            memcpy(&device,(void*)((uintptr_t)g_xbox_mem_offset+0x30c148u),4);
            if(width==1280 && height==720 && aspect==0x3fe38e39u && device==selected_720_device) {
                memcpy(stack+12,&width,4);memcpy(stack+16,&height,4);
                static unsigned reported;
                const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
                if(trace && !strcmp(trace,"1") && reported<8) {
                    ++reported;
                    fprintf(stderr,"[VIDEO-VIEWPORT-RESTORE] caller=000f07a2 device=%08x context=%08x original=640x480 selected=%ux%u\n",
                        device,selected_720_context,width,height);
                }
            }
        }
    }
    __real_sub_0006D150();
}

/* Read-only ownership query for the separately scoped original RUI resize. */
int topspin_game_video_selected_size(const char *label,uint32_t out[2])
{
    const char *wide=getenv("RECOMP_VIDEO_WIDESCREEN"),*mode=getenv("RECOMP_VIDEO_MODE"),*off=getenv("TOPSPIN_NATIVE720");
    if(!label||strcmp(label,"GAME")||!out||!selected_720_context||!selected_720_device
       ||!wide||strcmp(wide,"1")||!mode||strcmp(mode,"720p")||(off&&!strcmp(off,"0")))return 0;
    uint32_t size[2],device,aspect;
    if(selected_720_context<0x10000u||(uint64_t)selected_720_context+12u>0x4000000u
       ||selected_720_device<0x10000u||(uint64_t)selected_720_device+0x198u>0x4000000u)return 0;
    memcpy(size,(void*)((uintptr_t)g_xbox_mem_offset+selected_720_context+4),sizeof size);
    memcpy(&device,(void*)((uintptr_t)g_xbox_mem_offset+0x30c148u),4);
    memcpy(&aspect,(void*)((uintptr_t)g_xbox_mem_offset+selected_720_device+0x194u),4);
    if(size[0]!=1280||size[1]!=720||device!=selected_720_device||aspect!=0x3fe38e39u)return 0;
    memcpy(out,size,sizeof size);return 1;
}
