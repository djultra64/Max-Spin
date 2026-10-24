/* Owned prompt texture lifetime; integration work by djultra64. */
#ifndef TOPSPIN_CONTROLLER_RUI_LIFETIME_H
#define TOPSPIN_CONTROLLER_RUI_LIFETIME_H
#include <stddef.h>
#include <stdint.h>
#define TOPSPIN_RUI_LIVE_RESOURCES 32
#define TOPSPIN_RUI_LIVE_VARIANTS 64
typedef struct {
 void *user;
 /* Upload immutable owned XPR. On success return a distinct guest header and
  * allocation token; on failure retain no allocation. Original title headers
  * are never supplied as writable storage. */
 int (*upload)(void*,const void*,size_t,uint32_t*,uintptr_t*);
 /* Must stop future use of these headers (including title binding shadows),
  * complete queued PB/backend work, and only then return success. A renderer
  * fence alone without PB drain or title-cache invalidation is insufficient. */
 int (*quiesce)(void*,const uint32_t*,size_t);
 void (*release)(void*,uintptr_t);
} TopSpinRuiLifetimeOps;
typedef struct TopSpinRuiLifetime TopSpinRuiLifetime;
TopSpinRuiLifetime *topspin_rui_lifetime_create(const TopSpinRuiLifetimeOps*,size_t budget);
int topspin_rui_lifetime_attach(TopSpinRuiLifetime*,uint32_t object,uint32_t original_header);
int topspin_rui_lifetime_publish(TopSpinRuiLifetime*,uint32_t object,const uint8_t labels[4],const void*xpr,size_t bytes);
/* Consumer uses returned header as the argument to the original title bind.
 * It must preserve the original resource/header and pass the original when
 * there is no supported projection; caller must not claim UI completeness. */
uint32_t topspin_rui_lifetime_bind(const TopSpinRuiLifetime*,uint32_t original_header,const uint8_t labels[4]);
/* Retire removes consumer mappings immediately. Collection is explicit and
 * cannot free GPU-visible headers until the quiesce contract succeeds. */
void topspin_rui_lifetime_retire(TopSpinRuiLifetime*,uint32_t object);
int topspin_rui_lifetime_collect(TopSpinRuiLifetime*);
/* Retires all and quiesces; on failure leaves state/allocations alive so the
 * caller can retry after a real drain. Never silently frees in-flight data. */
int topspin_rui_lifetime_destroy(TopSpinRuiLifetime*);
size_t topspin_rui_lifetime_bytes(const TopSpinRuiLifetime*);
#endif
