/* NV_PGRAPH_SURFACE is shared by the command executor and the guest ISR.
 * Keep one atomic register; a guest INCREMENT_READ_3D acknowledges scanout.
 * Own integration: djultra64. No emulator implementation is copied here. */
#include "kernel.h"
#include "nv2a_flip.h"
static volatile LONG surface, enabled;
uint32_t nv2a_flip_surface(void) {
    return (uint32_t)InterlockedCompareExchange(&surface,0,0);
}
void nv2a_flip_surface_write(uint32_t value) {
    InterlockedExchange(&surface,(LONG)value);
}
void nv2a_flip_enable(int on) {
    if(on) nv2a_flip_surface_write(0);
    InterlockedExchange(&enabled,on!=0);
}
int nv2a_flip_enabled(void) {
    return InterlockedCompareExchange(&enabled,0,0)!=0;
}
static void set_field(unsigned shift,uint32_t value) {
    uint32_t before,after;
    do {
        before=nv2a_flip_surface();
        after=(before&~(7u<<shift))|((value&7u)<<shift);
    } while((uint32_t)InterlockedCompareExchange(&surface,(LONG)after,(LONG)before)!=before);
}
void nv2a_flip_set_read(uint32_t v) { set_field(24,v); }
void nv2a_flip_set_write(uint32_t v) { set_field(20,v); }
void nv2a_flip_set_modulo(uint32_t v) { set_field(28,v); }
static void advance(unsigned shift) {
    uint32_t before,after,value,modulo;
    do {
        before=nv2a_flip_surface();
        modulo=(before>>28)&7u;
        value=((before>>shift)&7u)+1u;
        if(modulo) value%=modulo;
        after=(before&~(7u<<shift))|((value&7u)<<shift);
    } while((uint32_t)InterlockedCompareExchange(&surface,(LONG)after,(LONG)before)!=before);
}
void nv2a_flip_increment_read(void) { advance(24); }
void nv2a_flip_increment_write(void) { advance(20); }
int nv2a_flip_stalled(void) {
    uint32_t s=nv2a_flip_surface();
    return nv2a_flip_enabled() && ((s>>28)&7u) && (((s>>24)&7u)==((s>>20)&7u));
}
void nv2a_flip_wait(void) {
    while(nv2a_flip_stalled()) Sleep(1);
}
