/* Original Menu RUI canvas sizing at its one initialization call site.
 * Own work: djultra64. Requires deterministic manual-hook routing because
 * original2FAD0 and its caller30520 share a generated translation unit. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "native_video.h"
extern _Thread_local uint32_t g_esp,g_ecx;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_0002FAD0_gen(void);
static int words(uint32_t va,uint32_t *out,size_t n)
{
    if(va<0x10000u||n>0x4000000u/4u||(uint64_t)va+n*4u>0x4000000u)return 0;
    memcpy(out,(void*)((uintptr_t)g_xbox_mem_offset+va),n*4u);return 1;
}
void sub_0002FAD0(void)
{
    uint32_t args[4],manager=0,dimensions[2],context=g_ecx;
    int applied=0;
    /*30520's real CALL30B18 follows PUSH0/PUSH480/PUSH640. Do not alter
     * another resize request, another manager or a failed native allocation. */
    if(topspin_native_video_selected_size("MENU",dimensions)
       &&words(g_esp,args,4)&&args[0]==0x00030b1du
       &&args[1]==640&&args[2]==480&&args[3]==0
       &&context>=0x10000u&&(uint64_t)context+0x1458u<=0x4000000u
       &&words(0x002b969cu,&manager,1)&&manager==context){
        memcpy((void*)((uintptr_t)g_xbox_mem_offset+g_esp+4),dimensions,sizeof dimensions);
        applied=1;
    }
    /* Original updates the viewport resource, authored-canvas mapping and
     * overscan/safe-area data together. Preserve its result and all ABI effects. */
    sub_0002FAD0_gen();
    static unsigned reported;
    const char *trace=getenv("TOPSPIN_VIDEO_TRACE");
    if(applied&&trace&&!strcmp(trace,"1")&&reported<4){
        uint32_t selected[2]={0};int valid=words(context+0x1420u,selected,2);
        ++reported;
        fprintf(stderr,"[MENU-RUI-CANVAS] caller=00030b1d manager=%08x original=640x480 requested=%ux%u original_overscan=0 result_valid=%d canvas=%ux%u\n",
                context,dimensions[0],dimensions[1],valid,selected[0],selected[1]);
    }
}
