/* Bounded Top Spin hair evidence, own work djultra64. No renderer calls. */
#ifndef NV2A_PB_HAIR_CAPTURE_H
#define NV2A_PB_HAIR_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#define PB_HAIR_CAPTURE_FRAMES 7u
#define PB_HAIR_CAPTURE_DRAWS 8u
#define PB_HAIR_CAPTURE_FRAME_BYTES (8u*1024u*1024u)
#define PB_HAIR_CAPTURE_TOTAL_BYTES (56u*1024u*1024u)
typedef struct {
    uint32_t vsh,color,format,pitch,clip_x,clip_y,clip_w,clip_h;
    uint32_t alpha_test,alpha_func,alpha_ref;
    uint32_t texture_valid,texture_format,texture_w,texture_h;
} PbHairDrawState;
/* Validated Pioline native haircut 08 signature, independent of guest pointers,
 * mesh index counts, head location, camera distance and CPU point coverage.
 * Main full court surface excludes stadium, shadow and replay subviews. */
static inline int pb_hair_draw_match(const PbHairDrawState*d) {
    return d->vsh==0xA02A4579u && d->color && (d->format&0xffffu)==0x0128u
        && d->pitch==5120u && !d->clip_x && !d->clip_y && d->clip_w==1280u && d->clip_h==720u
        && d->alpha_test && d->alpha_func==0x206u && d->texture_valid
        && d->texture_format==0x0fu && d->texture_h==256u
        && ((d->texture_w==128u && (d->alpha_ref==128u || d->alpha_ref==10u))
            || (d->texture_w==512u && d->alpha_ref==4u));
}
typedef struct { uint32_t min_flip,anchor,started,completed; } PbHairSchedule;
static inline int pb_hair_schedule_due(PbHairSchedule*s,uint32_t flip) {
    static const uint32_t offsets[PB_HAIR_CAPTURE_FRAMES]={0,30,60,120,240,480,720};
    if(s->completed>=PB_HAIR_CAPTURE_FRAMES || flip<s->min_flip)return 0;
    if(!s->started){s->started=1;s->anchor=flip;}
    return (uint64_t)flip>=(uint64_t)s->anchor+offsets[s->completed];
}
static inline void pb_hair_schedule_complete(PbHairSchedule*s) {
    if(s->completed<PB_HAIR_CAPTURE_FRAMES)s->completed++;
}
/* Reject before allocating: unlike the point ring, hair evidence never evicts
 * an earlier alpha pass silently. Counters in each manifest explain omissions. */
static inline int pb_hair_payload_fits(size_t frame,unsigned draws,size_t bytes,size_t all) {
    return bytes && draws<PB_HAIR_CAPTURE_DRAWS && frame<=PB_HAIR_CAPTURE_FRAME_BYTES
        && bytes<=PB_HAIR_CAPTURE_FRAME_BYTES-frame && all<=PB_HAIR_CAPTURE_TOTAL_BYTES
        && bytes<=PB_HAIR_CAPTURE_TOTAL_BYTES-all;
}
#endif
