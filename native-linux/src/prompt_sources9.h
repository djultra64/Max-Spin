/* PROPOSED coherent applied mapping; not linked into frontend. djultra64. */
#ifndef TOPSPIN_PROPOSED_SOURCES9_H
#define TOPSPIN_PROPOSED_SOURCES9_H
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "input/input_map.h"
_Static_assert(INPUT_SRC_COUNT<=64,"nine sources require6bit encoding");
/* First six retain the current published ABI. Slot6 is an INPUT_STICK_* tag,
 * not an InputSource or thumb-click; slot7 Back; slot8 Start. */
typedef struct { _Atomic uint64_t packed; } TopspinSources9;
static inline int topspin_sources9_publish(TopspinSources9*s,const InputPadMap*m){
 static const unsigned slots[8]={INPUT_A,INPUT_B,INPUT_X,INPUT_Y,INPUT_BLACK,INPUT_WHITE,INPUT_BACK,INPUT_START};
 if(!s||!m||m->right_stick>INPUT_STICK_NONE)return 0;
 uint8_t v[9];for(unsigned i=0;i<8;i++){
  unsigned n=i<6?i:i+1;
  if(m->source[slots[i]]>=INPUT_SRC_COUNT)return 0;
  v[n]=m->source[slots[i]];
 }
 v[6]=m->right_stick;uint64_t p=UINT64_C(1)<<63;
 for(unsigned i=0;i<9;i++)p|=(uint64_t)v[i]<<(i*6);
 atomic_store_explicit(&s->packed,p,memory_order_release);return 1;
}
static inline int topspin_sources9_read(TopspinSources9*s,uint8_t out[9]){
 if(!s||!out)return 0;uint64_t p=atomic_load_explicit(&s->packed,memory_order_acquire);
 if(!(p>>63))return 0;
 for(unsigned i=0;i<9;i++)out[i]=(uint8_t)((p>>(6*i))&63);return 1;
}
/* A new nine-label key is required; six equal faces cannot alias changed
 * Start/Back/stick variants. Caller uses only this after full validation. */
static inline int topspin_sources9_same(const uint8_t a[9],const uint8_t b[9]){return !memcmp(a,b,9);}
/* Resource-specific key: skip unavailable cells and other sprite slots.
 * Thus changing Start does not allocate duplicate AButton variants. */
static inline void topspin_sources9_key(const uint8_t labels[9],unsigned mask,uint8_t key[9]){
 for(unsigned i=0;i<9;i++)key[i]=(mask&(1u<<i))?labels[i]:0;
}
/* Same-size RGBA font texture as existing projector; no larger atlas/metrics.
 * Guard subtraction before allocating; existing pinned objects never evicted. */
static inline int topspin_sources9_budget(uint32_t w,uint32_t h,size_t pinned,size_t budget,size_t*out){
 if(!out||!w||!h||w>2048||h>2048||pinned>budget)return 0;
 size_t n=((size_t)128+(size_t)w*h*4+4095)&~(size_t)4095;
 if(n>budget-pinned)return 0;*out=n;return 1;
}
#endif
