/* Shared title framework native720 guards. Own work: djultra64. */
#include "native_video.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern _Thread_local uint32_t g_esp,g_eax,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
static uint32_t native_app,native_device;
static const TopspinNativeVideoProfile *native_profile;
static _Thread_local uint32_t native_constructor;
static unsigned trace_lines;
static int enabled(void)
{
    const char *value=getenv("TOPSPIN_VIDEO_TRACE");
    return value && !strcmp(value,"1");
}
static int words(uint32_t va,uint32_t *out,size_t count)
{
    if(va<0x10000u || count>0x4000000u/4u
       || (uint64_t)va+count*4u>0x4000000u)return 0;
    memcpy(out,(const void*)((uintptr_t)g_xbox_mem_offset+va),count*4u);
    return 1;
}


static void store(uint32_t va,uint32_t value)
{
    memcpy((void*)((uintptr_t)g_xbox_mem_offset+va),&value,4);
}
static int native_gate(const TopspinNativeVideoProfile *p)
{
    const char *wide=getenv("RECOMP_VIDEO_WIDESCREEN");
    const char *mode=getenv("RECOMP_VIDEO_MODE");
    const char *optout=getenv("TOPSPIN_NATIVE720");
    const char *legacy=!strcmp(p->label,"MENU")?getenv("TOPSPIN_MENU_NATIVE720"):NULL;
    uint32_t record[4];
    /* Verify this original image's record8 before requesting native720. */
    return wide && !strcmp(wide,"1") && mode && !strcmp(mode,"720p")
        && !(optout && !strcmp(optout,"0"))
        && !(legacy && !strcmp(legacy,"0"))
        && words(p->record8,record,4)
        && record[0]==1280 && record[1]==720 && record[2]==1 && record[3]==1;
}
static int native_owner(const TopspinNativeVideoProfile *p)
{
    uint32_t app=0,device=0,output=0,pp[17];
    return native_profile==p && native_app && native_device && native_gate(p)
        && words(p->app_global,&app,1) && app==native_app
        && words(p->xdk_device_global,&device,1) && device==native_device
        && words(app+0x4cu,&output,1) && output==native_device
        && words(app+4u,pp,17) && pp[0]==1280 && pp[1]==720
        && pp[2]==6 && pp[4]==0x1021 && (pp[10]&0x70u)==0x50u;
}

void topspin_native_video_create(const TopspinNativeVideoProfile *p,void (*original)(void))
{
    /* Full-body profile proves stdcall CreateDevice(...,PP,Out), with
     * PP=context+4 and Out=context+4C on this exact title caller. */
    uint32_t arguments[7]={0},before[17]={0},after[17]={0};
    uint32_t automatic[4]={0},device=0,app=0;
    int observe=enabled() && trace_lines<16;
    int stack_valid=0,pp_valid=0,after_valid=0,out_valid=0,auto_valid=0;
    int native_attempt=0,native_accepted=0;
    /* Read only when tracing or the user requests native720. */
    if(observe || native_gate(p)) {
        stack_valid=words(g_esp,arguments,7);
        if(stack_valid)pp_valid=words(arguments[5],before,17);
        auto_valid=words(p->automatic_info_global,automatic,4);
        (void)words(p->app_global,&app,1);
    }
    if(stack_valid && arguments[0]==p->create_return) {
        native_app=native_device=0;native_profile=NULL;
        uint32_t prior_output=0;
        if(native_gate(p) && pp_valid && auto_valid
           && app>=0x10000u && (uint64_t)app+0x50u<=0x4000000u
           && arguments[5]==app+4u && arguments[6]==app+0x4cu
           && words(arguments[6],&prior_output,1)
           && before[0]==640 && before[1]==480 && before[2]==6
           && before[4]==0x1021 && before[10]==0 && before[12]==0x80000001u
           && (automatic[0]&0x4500u)==0x4500u
           && automatic[1]==1280 && automatic[2]==720) {
            store(arguments[5],1280);store(arguments[5]+4u,720);
            /* Same original record8 derivation as the title's selector:
             * progressive=0x40, widescreen=0x10. Preserve AA and interval. */
            store(arguments[5]+0x28u,0x50u);
            native_attempt=1;
        }
    }
    original();
    uint32_t result=g_eax;
    if(observe || native_attempt) {
        if(stack_valid) {
            after_valid=words(arguments[5],after,17);
            out_valid=words(arguments[6],&device,1);
        }
        if(native_attempt && result==0 && after_valid && out_valid
           && device>=0x10000u && device<0x4000000u
           && after[0]==1280 && after[1]==720 && after[4]==0x1021
           && (after[10]&0x70u)==0x50u) {
            native_app=app;native_device=device;native_profile=p;
            native_accepted=native_owner(p);
            if(!native_accepted){native_app=native_device=0;native_profile=NULL;}
        }
    }
    if(observe) {
        ++trace_lines;
        fprintf(stderr,"[%s-VIDEO-CREATE] caller=%08x stack_valid=%d pp=%08x pp_valid=%d before=%ux%u format=%08x multisample=%08x flags=%08x refresh=%u interval=%08x after_valid=%d after=%ux%u after_format=%08x after_multisample=%08x after_flags=%08x after_refresh=%u after_interval=%08x out=%08x out_valid=%d device=%08x hresult=%08x app=%08x auto_valid=%d auto_flags=%08x auto_size=%ux%u auto_refresh_raw=%08x native_attempt=%d native_accepted=%d\n",
                p->label,arguments[0],stack_valid,arguments[5],pp_valid,before[0],before[1],before[2],before[4],before[10],before[11],before[12],after_valid,after[0],after[1],after[2],after[4],after[10],after[11],after[12],arguments[6],out_valid,device,result,app,auto_valid,automatic[0],automatic[1],automatic[2],automatic[3],native_attempt,native_accepted);
    }
}

void topspin_native_video_viewport(const TopspinNativeVideoProfile *p,void (*original)(void))
{
    /* Full546-byte setter bodies and exact title call edges were verified
     * independently for each image. Offscreen caller/pointer tuples survive. */
    static uint32_t seen[16][7];
    static unsigned count;
    uint32_t arguments[2]={0},tuple[7]={0};
    int observe=enabled() && trace_lines<16,valid=0,native_apply=0;
    if(observe || native_constructor) {
        if(words(g_esp,arguments,2))valid=words(arguments[1],tuple+1,6);
        tuple[0]=arguments[0];
        if(valid && native_constructor && native_owner(p)
           && arguments[0]==p->viewport_return
           && arguments[1]==native_constructor+0x17cu
           && tuple[1]==0 && tuple[2]==0 && tuple[3]==640 && tuple[4]==480
           && tuple[5]==0 && tuple[6]==0x3f800000u) {
            store(arguments[1]+8u,1280);store(arguments[1]+12u,720);
            tuple[3]=1280;tuple[4]=720;native_apply=1;
        }
        for(unsigned i=0;i<count;i++)
            if(!memcmp(seen[i],tuple,sizeof tuple)){observe=0;break;}
    }
    original();
    if(observe && count<16) {
        memcpy(seen[count++],tuple,sizeof tuple);++trace_lines;
        fprintf(stderr,"[%s-VIDEO-VIEWPORT] caller=%08x pointer=%08x valid=%d rect=%u,%u,%u,%u z_raw=%08x/%08x native_apply=%d\n",
                p->label,arguments[0],arguments[1],valid,tuple[1],tuple[2],tuple[3],tuple[4],tuple[5],tuple[6],native_apply);
    }
}


void topspin_native_video_constructor(const TopspinNativeVideoProfile *p,void (*original)(void))
{
    uint32_t caller=0,context=g_ecx,previous=native_constructor;
    int eligible=native_owner(p) && words(g_esp,&caller,1)
        && caller==p->constructor_return && context>=0x10000u
        && (uint64_t)context+0x1a0u<=0x4000000u;
    /* Adapter integration must verify the real cross-TU call route (or use
     * deterministic routing); profile selection alone cannot ensure LDwrap. */
    native_constructor=eligible?context:0;
    original();
    native_constructor=previous;
    if(eligible && g_eax==context && native_owner(p)) {
        uint32_t primary=0,viewport[6],aspect=0;
        if(words(p->primary_camera_global,&primary,1) && primary==context
           && words(context+0x17cu,viewport,6)
           && viewport[0]==0 && viewport[1]==0
           && viewport[2]==1280 && viewport[3]==720
           && words(context+0x194u,&aspect,1) && aspect==0x3faaaaabu) {
            /* The original constructor writes4:3 AFTER SetViewport. Correct
             * only this newly constructed primary camera after it returns. */
            store(context+0x194u,0x3fe38e39u);
            if(enabled() && trace_lines<16) {
                ++trace_lines;
                fprintf(stderr,"[%s-VIDEO-CAMERA] caller=%08x context=%08x viewport=1280x720 aspect_before=%08x aspect_after=3fe38e39\n",p->label,caller,context,aspect);
            }
        }
    }
}

/* Used by image-scoped canvas adapters only after the real native allocation.
 * Every query revalidates live title/device ownership and original gates. */
int topspin_native_video_selected_size(const char *label,uint32_t out[2])
{
    if(!native_profile || !label || !out || strcmp(native_profile->label,label)
       || !native_owner(native_profile))return 0;
    out[0]=1280;out[1]=720;return 1;
}
