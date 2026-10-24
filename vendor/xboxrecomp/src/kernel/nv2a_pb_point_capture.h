/* Read-only point diagnostic helpers. Own work: djultra64.
 * No renderer calls, guest reads, environment access, or initialization work.
 * Callers must gate every use behind their opt-in diagnostic flag. */
#ifndef NV2A_PB_POINT_CAPTURE_H
#define NV2A_PB_POINT_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PB_POINT_CAPTURE_SLOTS 8u
#define PB_POINT_CAPTURE_BUDGET (8u*1024u*1024u)
#define PB_POINT_CAPTURE_MANIFEST_LIMIT 256u

typedef struct {uint64_t sequence;size_t bytes;unsigned char *data;} PbPointCaptureSlot;
typedef struct {
    PbPointCaptureSlot slots[PB_POINT_CAPTURE_SLOTS];
    unsigned first,count;size_t bytes,pending_bytes,limit_bytes;unsigned char *pending;
    uint64_t offered,evicted,oversize,allocation_failed,invalid;
} PbPointCaptureRing;
static inline const PbPointCaptureSlot *pb_point_ring_at(const PbPointCaptureRing*r,unsigned ordinal)
{return ordinal<r->count?&r->slots[(r->first+ordinal)%PB_POINT_CAPTURE_SLOTS]:NULL;}
static inline void pb_point_ring_evict(PbPointCaptureRing*r)
{
    if(!r->count)return;
    PbPointCaptureSlot*s=&r->slots[r->first];r->bytes-=s->bytes;free(s->data);memset(s,0,sizeof *s);
    r->first=(r->first+1)%PB_POINT_CAPTURE_SLOTS;r->count--;r->evicted++;
}
/* Reserve before building the payload. Retained blobs plus this writable
 * reservation never exceed the global budget; commit transfers ownership
 * without a second copy or allocation. A failed reservation is an explicit loss. */
static inline unsigned char *pb_point_ring_begin(PbPointCaptureRing*r,size_t bytes)
{
    r->offered++;
    if(r->pending||!bytes){r->invalid++;return NULL;}
    size_t limit=r->limit_bytes?r->limit_bytes:PB_POINT_CAPTURE_BUDGET;
    if(limit>PB_POINT_CAPTURE_BUDGET)limit=PB_POINT_CAPTURE_BUDGET;
    if(bytes>limit){r->oversize++;return NULL;}
    while(r->count&&(r->count==PB_POINT_CAPTURE_SLOTS||bytes>limit-r->bytes))pb_point_ring_evict(r);
    r->pending=(unsigned char*)malloc(bytes);
    if(!r->pending){r->allocation_failed++;return NULL;}
    r->pending_bytes=bytes;return r->pending;
}
static inline int pb_point_ring_commit(PbPointCaptureRing*r,uint64_t sequence)
{
    if(!r->pending){r->invalid++;return 0;}
    PbPointCaptureSlot*s=&r->slots[(r->first+r->count)%PB_POINT_CAPTURE_SLOTS];
    s->sequence=sequence;s->bytes=r->pending_bytes;s->data=r->pending;
    r->count++;r->bytes+=r->pending_bytes;r->pending=NULL;r->pending_bytes=0;return 1;
}
static inline void pb_point_ring_cancel(PbPointCaptureRing*r)
{
    if(r->pending){free(r->pending);r->pending=NULL;r->pending_bytes=0;r->invalid++;}
}
static inline void pb_point_ring_clear(PbPointCaptureRing*r)
{
    /* Cleanup is not an overwritten-payload loss. Preserve diagnostic totals. */
    uint64_t evicted=r->evicted;
    while(r->count)pb_point_ring_evict(r);
    pb_point_ring_cancel(r);r->evicted=evicted;r->first=0;
}

/* Each complete payload is a sequence of [u32 nameBytes,u32 dataBytes,
 * suffix including NUL,data]. Names are fixed caller-owned suffixes, never
 * guest filenames. memcpy avoids unaligned word accesses while walking blobs. */
static inline size_t pb_point_file_size(const char*name,size_t bytes)
{
    size_t n=strlen(name)+1;
    return n<=64&&bytes<=PB_POINT_CAPTURE_BUDGET?8+n+bytes:0;
}
static inline int pb_point_file_append(unsigned char*blob,size_t capacity,size_t*used,
                                      const char*name,const void*data,size_t bytes)
{
    size_t total=pb_point_file_size(name,bytes);
    if(!total||*used>capacity||total>capacity-*used||(!data&&bytes))return 0;
    uint32_t sizes[2]={(uint32_t)strlen(name)+1,(uint32_t)bytes};
    memcpy(blob+*used,sizes,8);memcpy(blob+*used+8,name,sizes[0]);
    if(bytes)memcpy(blob+*used+8+sizes[0],data,bytes);
    *used+=total;return 1;
}
/* Return one complete record or zero for malformed/end. Caller checks offset
 * equals slot.bytes at successful end. suffix excludes '/' and '..' on export. */
static inline int pb_point_file_next(const unsigned char*blob,size_t bytes,size_t*offset,
                                    const char**name,const unsigned char**data,size_t*data_bytes)
{
    uint32_t sizes[2];size_t at=*offset;
    if(at>bytes||bytes-at<8)return 0;
    memcpy(sizes,blob+at,8);
    if(!sizes[0]||sizes[0]>64||sizes[0]>bytes-at-8||sizes[1]>bytes-at-8-sizes[0])return 0;
    const char*n=(const char*)blob+at+8;
    if(n[sizes[0]-1]||strlen(n)+1!=sizes[0]||strchr(n,'/')||strstr(n,".."))return 0;
    *name=n;*data=blob+at+8+sizes[0];*data_bytes=sizes[1];
    *offset=at+8+sizes[0]+sizes[1];return 1;
}

typedef struct {
    double clip_x,clip_y,clip_width,clip_height,zmax;
    uint32_t cull_enabled,front_face,cull_face;
} PbPointCaptureState;
typedef enum {
    PB_POINT_HIT,PB_POINT_OUTSIDE,PB_POINT_CLIP,PB_POINT_CULLED,
    PB_POINT_NONFINITE,PB_POINT_W_CROSSING,PB_POINT_DEGENERATE,PB_POINT_DEPTH
} PbPointCaptureResult;
/* Positions are already in guest screen coordinates, including viewport scale.
 * W must be positive (PgraphReadyVertex already stores clip-space W). W-crossing
 * triangles are reported unresolved, not silently counted as absent. This is
 * sample-center coverage, not a replacement for rasterizer clipping/top-left
 * edge rules or a depth-buffer visibility claim. */
static inline PbPointCaptureResult pb_point_triangle(const float p[3][4],double px,double py,
                                                     const PbPointCaptureState*s,double*zout)
{
    if(px<s->clip_x||py<s->clip_y||px>=s->clip_x+s->clip_width||py>=s->clip_y+s->clip_height)return PB_POINT_CLIP;
    for(unsigned i=0;i<3;i++){
        for(unsigned j=0;j<4;j++)if(!isfinite(p[i][j]))return PB_POINT_NONFINITE;
        if(p[i][3]<=0)return PB_POINT_W_CROSSING;
    }
    double den=((double)p[1][1]-p[2][1])*((double)p[0][0]-p[2][0])
              +((double)p[2][0]-p[1][0])*((double)p[0][1]-p[2][1]);
    if(fabs(den)<1e-12)return PB_POINT_DEGENERATE;
    if(s->cull_enabled){int front=s->front_face==0x900?den>0:den<0;
        if(s->cull_face==0x408||(s->cull_face==0x405&&!front)||(s->cull_face==0x404&&front))return PB_POINT_CULLED;}
    double a=(((double)p[1][1]-p[2][1])*(px-p[2][0])+((double)p[2][0]-p[1][0])*(py-p[2][1]))/den;
    double b=(((double)p[2][1]-p[0][1])*(px-p[2][0])+((double)p[0][0]-p[2][0])*(py-p[2][1]))/den;
    double c=1-a-b;if(a<0||b<0||c<0)return PB_POINT_OUTSIDE;
    double z=a*p[0][2]+b*p[1][2]+c*p[2][2];if(z<0||z>s->zmax)return PB_POINT_DEPTH;
    if(zout)*zout=z;
    return PB_POINT_HIT;
}
#endif
