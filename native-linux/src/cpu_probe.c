/* Top Spin XDK 5558 D3DX CPU probe compatibility. Own work: djultra64.
 * Only the hash-validated ID-bit/MMX probe is supported here. Its arithmetic
 * flags never escape or decide a branch before the probe's own XOR. Generic
 * PUSHFD/POPFD still require a complete guest EFLAGS model in the translator.
 * CPU profile reference: xemu-project/xemu target/i386/cpu.c, pentium3/XBOX.
 */
#include "cpu_probe.h"
#include <string.h>
extern _Thread_local uint32_t g_eax,g_ebx,g_ecx,g_edx,g_esp;
static int validated;
static uint32_t probe_address;
static _Thread_local uint32_t control_flags=0x202;
static _Thread_local unsigned phase;

int topspin_cpu_probe_validate(uint8_t *memory) {
    return topspin_cpu_probe_validate_at(memory,0x1d3d75);
}

int topspin_cpu_probe_validate_at(uint8_t *memory,uint32_t address) {
    validated=0;
    probe_address=0;
    phase=0; control_flags=0x202;
    if(!memory || address<0x10000 || address>0x04000000u-50) return 0;
    uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for(unsigned i=0;i<50;i++) hash=(hash^memory[address+i])*UINT64_C(0x100000001b3);
    validated=hash==UINT64_C(0x5f5b18685e4b3ac7);
    if(validated) probe_address=address;
    return validated;
}

int topspin_cpu_probe_instruction(const char *text,uint32_t va,uint8_t *memory) {
    if(!validated || !memory || g_esp<0x10004 || g_esp>0x03fffffc) return 0;
    if((va==probe_address+6 && phase==0) || (va==probe_address+18 && phase==2)) {
        if(strcmp(text,"pushfd")) return 0;
        g_esp-=4;
        memcpy(memory+g_esp,&control_flags,4);
        phase++;
        return 1;
    }
    if(va==probe_address+17 && phase==1 && !strcmp(text,"popfd")) {
        memcpy(&control_flags,memory+g_esp,4);
        g_esp+=4;
        /* In this routine the only changed flag is CPUID's ID bit. */
        control_flags=(control_flags&0x200000u)|0x202u;
        phase=2;
        return 1;
    }
    if(va==probe_address+29 && phase==3 && !strcmp(text,"cpuid") && g_eax==1) {
        /* Stable Xbox profile: family 6/model 8/stepping 10, MMX and SSE.
         * No host feature passthrough; SSE2/AVX cannot leak from the Ryzen. */
        g_eax=0x68a; g_ebx=0; g_ecx=0; g_edx=0x0383fbfb;
        phase=0;
        return 1;
    }
    return 0;
}
