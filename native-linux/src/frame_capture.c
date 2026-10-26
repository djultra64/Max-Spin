/* Native Vulkan backbuffer diagnostics. Own work: djultra64. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "frame_capture.h"
#include "d3d8_vk_rt.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void le16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void le32(unsigned char *p,uint32_t v) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i)); }

/* This runtime's sf_LockRect explicitly returns RGBA bytes, even though
 * GetDesc names A8R8G8B8. See d3d8_vk.c's backbuffer implementation. BMP BI_RGB
 * stores BGRA; use top-down rows and preserve RGB exactly with opaque alpha. */
static int write_bmp(const char *path,const D3DSURFACE_DESC *desc,const D3DLOCKED_RECT *lock,int exclusive)
{
    uint64_t rowbytes=(uint64_t)desc->Width*4,bytes=rowbytes*desc->Height;
    unsigned char header[54]={0},*row;
    char temporary[1100];
    FILE *f;
    int ok=1;
    if(desc->Format!=D3DFMT_A8R8G8B8 || !desc->Width || !desc->Height ||
       desc->Width>INT32_MAX || desc->Height>INT32_MAX || bytes>UINT32_MAX-54 ||
       lock->Pitch<0 || (uint64_t)lock->Pitch<rowbytes || !lock->pBits)
        return 0;
    if(snprintf(temporary,sizeof temporary,"%s.tmp",path)>=(int)sizeof temporary)
        return 0;
    row=malloc((size_t)rowbytes);
    if(!row)return 0;
    if(exclusive){
#if !defined(_WIN32)
        int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        f=fd<0?NULL:fdopen(fd,"wb");
        if(fd>=0&&!f){close(fd);remove(temporary);}
#else
        free(row);return 0; /* Replay event captures are POSIX-only. */
#endif
    }else f=fopen(temporary,"wb");
    if(!f){free(row);return 0;}
    header[0]='B';header[1]='M';le32(header+2,(uint32_t)bytes+54);le32(header+10,54);
    le32(header+14,40);le32(header+18,desc->Width);le32(header+22,0u-desc->Height);
    le16(header+26,1);le16(header+28,32);le32(header+34,(uint32_t)bytes);
    if(fwrite(header,1,sizeof header,f)!=sizeof header)ok=0;
    for(unsigned y=0;ok && y<desc->Height;y++){
        const unsigned char *src=(const unsigned char*)lock->pBits+(size_t)y*lock->Pitch;
        for(unsigned x=0;x<desc->Width;x++){
            row[x*4]=src[x*4+2];row[x*4+1]=src[x*4+1];row[x*4+2]=src[x*4];row[x*4+3]=255;
        }
        if(fwrite(row,1,(size_t)rowbytes,f)!=(size_t)rowbytes)ok=0;
    }
    if(fclose(f))ok=0;
    free(row);
    if(exclusive){
#if !defined(_WIN32)
        /* link is atomic and fails on an existing destination; unlike rename,
         * it cannot replace an earlier screenshot or follow a symlink. */
        if(ok && link(temporary,path))ok=0;
        remove(temporary);
#endif
    }else if(ok && rename(temporary,path))ok=0;
    if(!ok)remove(temporary);
    return ok;
}


static int capture_backbuffer_limited(IDirect3DDevice8 *device,const char *path,
                              D3DSURFACE_DESC *desc,HRESULT *api_result,int exclusive,uint64_t limit)
{
    IDirect3DSurface8 *surface=NULL;
    D3DLOCKED_RECT lock={0};
    memset(desc,0,sizeof *desc);
    int locked=0,ok=0;
    HRESULT result=device->lpVtbl->GetBackBuffer(device,0,0,&surface);
    if(result>=0 && surface){
        result=surface->lpVtbl->GetDesc(surface,desc);
        if(result>=0 && limit && (desc->Width>4096 || desc->Height>4096 ||
           (uint64_t)desc->Width*desc->Height*4u+54u>limit))
            result=(HRESULT)0x80004005u;
        if(result>=0){
            result=surface->lpVtbl->LockRect(surface,&lock,NULL,D3DLOCK_READONLY);
            if(result>=0){locked=1;ok=write_bmp(path,desc,&lock,exclusive);}
        }
        if(locked)surface->lpVtbl->UnlockRect(surface);
        surface->lpVtbl->Release(surface);
    }
    *api_result=result;return ok;
}
static int capture_backbuffer(IDirect3DDevice8 *device,const char *path,
                              D3DSURFACE_DESC *desc,HRESULT *api_result,int exclusive)
{
    return capture_backbuffer_limited(device,path,desc,api_result,exclusive,0);
}

/* The guest CPU publishes only bounded counters, never guest memory addresses.
 * The GPU executor owns readback, filename state, attempts and history output. */
#if !defined(_WIN32)
static atomic_int replay_enabled=ATOMIC_VAR_INIT(-1);
static atomic_uint replay_views=ATOMIC_VAR_INIT(0),replay_pending=ATOMIC_VAR_INIT(0);
static const unsigned replay_thresholds[]={1,4,16,64,128,256};
static int replay_capture_on(void)
{
    int enabled=atomic_load_explicit(&replay_enabled,memory_order_relaxed);
    if(enabled<0){
        const char *gate=getenv("TOPSPIN_REPLAY_RENDER_CAPTURE");
        enabled=gate&&!strcmp(gate,"1");
        atomic_store_explicit(&replay_enabled,enabled,memory_order_relaxed);
    }
    return enabled;
}
void topspin_frame_capture_replay_view(void)
{
    if(!replay_capture_on())return;
    unsigned view=atomic_load_explicit(&replay_views,memory_order_relaxed);
    do{if(view>=256)return;}
    while(!atomic_compare_exchange_weak_explicit(&replay_views,&view,view+1,
                                                memory_order_relaxed,memory_order_relaxed));
    view++;
    for(unsigned i=0;i<sizeof replay_thresholds/sizeof *replay_thresholds;i++)
        if(view==replay_thresholds[i]){
            atomic_fetch_or_explicit(&replay_pending,1u<<i,memory_order_release);
            break;
        }
}
static void capture_replay_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{
    static int initialized,enabled;static unsigned attempts;static char prefix[768];
    if(!replay_capture_on()||!device||attempts>=6)return;
    unsigned pending=atomic_exchange_explicit(&replay_pending,0,memory_order_acquire);
    if(!pending)return;
    if(!initialized){
        initialized=1;const char *p=getenv("TOPSPIN_CAPTURE_PREFIX");
        if(!p||p[0]!='/'||strlen(p)>=sizeof prefix){
            fputs("[REPLAY-RENDER-CAPTURE] invalid absolute capture prefix; disabled\n",stderr);return;
        }
        memcpy(prefix,p,strlen(p)+1);enabled=1;
    }
    if(!enabled)return;
    unsigned view=0;
    for(unsigned i=0;i<sizeof replay_thresholds/sizeof *replay_thresholds;i++)
        if(pending&(1u<<i))view=replay_thresholds[i];
    attempts++; /* Failures consume a bounded attempt; no repeated API/IO loop. */
    char path[1024];D3DSURFACE_DESC desc={0};HRESULT result=0;
    snprintf(path,sizeof path,"%s-replay-view%06u-frame%08llu.bmp",prefix,view,(unsigned long long)frame);
    int ok=capture_backbuffer(device,path,&desc,&result,1);
    fprintf(stderr,"[REPLAY-RENDER-CAPTURE] %s %s view=%u coalesced_mask=%02X attempt=%u actual=%.3fs frame=%llu size=%ux%u format=%08X api=%08X\n",
            ok?"saved":"FAILED",path,view,pending,attempts,(double)elapsed_ns/1e9,
            (unsigned long long)frame,desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)result);
    /* Synchronous post-Present history belongs to the same completed image.
     * Keep it even if readback/file creation failed, to retain that evidence. */
    extern void nv2a_pb_dump_present_history(uint64_t,uint64_t);
    nv2a_pb_dump_present_history(frame,elapsed_ns);
}
#else
void topspin_frame_capture_replay_view(void) { }
static void capture_replay_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{ (void)device;(void)elapsed_ns;(void)frame; }
#endif

/* Optional app-local requests; all state belongs to this GPU executor thread.
 * Request counters are unrelated to game clocks, frames or guest input. */
#if !defined(_WIN32)
static int request_counter(const unsigned char *data,size_t bytes,uint64_t *out)
{
 if(bytes && data[bytes-1]=='\n')bytes--;
 if(!bytes || bytes>16 || data[0]<'1'||data[0]>'9')return 0;
 uint64_t value=0;
 for(size_t i=0;i<bytes;i++){
  unsigned c=data[i];if(c<'0'||c>'9')return 0;
  value=value*10u+(c-'0'); /* <=16 digits: cannot overflow uint64_t */
 }
 *out=value;return 1;
}
static void capture_request_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{
 static int initialized,enabled,primed;static unsigned attempts,reported;
 static uint64_t last;static char path[1024],prefix[768];
 if(!initialized){
  const char *request=getenv("TOPSPIN_CAPTURE_REQUEST_FILE");initialized=1;
  if(!request)return; /* DefaultOFF: no file or renderer API calls. */
  const char *p=getenv("TOPSPIN_CAPTURE_PREFIX");
  if(!*request||request[0]!='/'||strlen(request)>=sizeof path||!p||!*p||strlen(p)>=sizeof prefix){
   fputs("[FRAME-CAPTURE-REQUEST] invalid absolute request path/prefix; disabled\n",stderr);return;
  }
  memcpy(path,request,strlen(request)+1);memcpy(prefix,p,strlen(p)+1);enabled=1;
 }
 if(!enabled||!device||attempts>=16)return;
 unsigned reason=0;uint64_t value=0;unsigned char data[17];
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
 if(fd<0){if(errno==ENOENT){primed=1;return;}reason=1;goto rejected;}
 struct stat before,after;
 if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_uid!=geteuid()||
    (before.st_mode&07777)!=0600||before.st_size<0||before.st_size>16){reason=2;close(fd);goto rejected;}
 if(before.st_size==0){close(fd);primed=1;return;} /* normal in-progress/initial value */
 ssize_t bytes=read(fd,data,sizeof data);
 int stat_ok=fstat(fd,&after)==0;close(fd);
 if(bytes<0||!stat_ok||bytes!=before.st_size||before.st_size!=after.st_size||
    before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||
    before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec){reason=3;goto rejected;}
 /* Zero is an initial idle marker, never a positive capture request. */
 if((bytes==1&&data[0]=='0')||(bytes==2&&data[0]=='0'&&data[1]=='\n')){primed=1;return;}
 if(!request_counter(data,(size_t)bytes,&value)){reason=4;goto rejected;}
 if(!primed){last=value;primed=1;return;} /* stale existing counter at child startup */
 if(value<=last)return;
 last=value;attempts++; /* consume before API/write failure: never retry same request */
 char output[1024];D3DSURFACE_DESC desc={0};HRESULT result=0;
 snprintf(output,sizeof output,"%s-request%08llu.bmp",prefix,(unsigned long long)value);
 int ok=capture_backbuffer(device,output,&desc,&result,0);
 fprintf(stderr,"[FRAME-CAPTURE-REQUEST] %s %s counter=%llu attempt=%u actual=%.3fs frame=%llu size=%ux%u format=%08X api=%08X\n",ok?"saved":"FAILED",output,(unsigned long long)value,attempts,(double)elapsed_ns/1e9,(unsigned long long)frame,desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)result);
 return;
rejected:
 if(!(reported&(1u<<reason))){reported|=1u<<reason;fprintf(stderr,"[FRAME-CAPTURE-REQUEST] ignored invalid request reason=%u (open=1,private-file=2,transient-read=3,counter=4)\n",reason);}
}
#else
static void capture_request_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{ (void)device;(void)elapsed_ns;(void)frame; }
#endif

/* Independent, defaultOFF capture of actual live procedural crowd images.
 * No pixel, stadium or hardcoded guest allocation selects an atlas. */
#if !defined(_WIN32)
extern int d3d8_vk_rt_diagnostic_rgb565(uint32_t *,uint32_t,uint32_t *) __attribute__((weak));
extern int nv2a_pb_hair_capture_completed_frame(uint32_t *) __attribute__((weak));
enum { CROWD_ATLASES=64,CROWD_SNAPSHOTS=12,CROWD_QUERIES=4096,CROWD_PIXELS=64*128*4 };
#define CROWD_RAW_BUDGET (UINT64_C(25)*1024*1024)
#define CROWD_BMP_BUDGET (UINT64_C(400)*1024*1024)
#define CROWD_BMP_SINGLE (UINT64_C(64)*1024*1024)
static int crowd_decimal(const char *value,uint64_t *out)
{
    if(!value || value[0]<'1'||value[0]>'9')return 0;
    uint64_t number=0;
    for(const unsigned char *p=(const unsigned char*)value;*p;p++) {
        if(*p<'0'||*p>'9'||number>(10000000u-(*p-'0'))/10u)return 0;
        number=number*10u+(*p-'0');
    }
    if(!number)return 0;
    *out=number;return 1;
}
static FILE *crowd_exclusive(const char *path)
{
    int descriptor=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(descriptor<0)return NULL;
    FILE *stream=fdopen(descriptor,"wb");
    if(!stream){close(descriptor);unlink(path);}
    return stream;
}
static int crowd_write(const char *path,const unsigned char *data,size_t bytes)
{
    FILE *stream=crowd_exclusive(path);
    if(!stream)return 0;
    int ok=fwrite(data,1,bytes,stream)==bytes;
    if(fclose(stream))ok=0;
    if(!ok)unlink(path); /* Only a file exclusively created by this call. */
    return ok;
}
static void capture_crowd_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{
    static int initialized,enabled,match_hair;
    static unsigned attempts,queries,hair_queries,exhausted_reported;
    static uint64_t minimum=1000,interval=60,next_frame,last_frame,bmp_written,raw_written;
    static char prefix[768],stem[129];
    if(!initialized) {
        initialized=1;
        const char *gate=getenv("TOPSPIN_CROWD_CAPTURE");
        if(!gate||strcmp(gate,"1"))return;
        const char *p=getenv("TOPSPIN_CROWD_CAPTURE_PREFIX");
        char directory[768];struct stat status;
        if(!p||p[0]!='/'||strlen(p)>=sizeof prefix)goto invalid;
        const char *slash=strrchr(p,'/');size_t length=strlen(slash+1);
        if(!length||length>=sizeof stem)goto invalid;
        for(const unsigned char *c=(const unsigned char*)slash+1;*c;c++)
            if(!((*c>='A'&&*c<='Z')||(*c>='a'&&*c<='z')||(*c>='0'&&*c<='9')||*c=='_'||*c=='-'||*c=='.'))goto invalid;
        size_t parent=(size_t)(slash-p);if(!parent)parent=1;
        memcpy(directory,p,parent);directory[parent]=0;
        int fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0)goto invalid;
        int private_ok=!fstat(fd,&status)&&status.st_uid==geteuid()&&!(status.st_mode&0077);
        close(fd);if(!private_ok)goto invalid;
        const char *value=getenv("TOPSPIN_CROWD_CAPTURE_MIN_FRAME");
        if(value&&!crowd_decimal(value,&minimum))goto invalid;
        value=getenv("TOPSPIN_CROWD_CAPTURE_INTERVAL_FRAMES");
        if(value&&!crowd_decimal(value,&interval))goto invalid;
        value=getenv("TOPSPIN_CROWD_CAPTURE_MATCH_HAIR");match_hair=value&&!strcmp(value,"1");
        if(!d3d8_vk_rt_diagnostic_rgb565 || (match_hair&&!nv2a_pb_hair_capture_completed_frame))goto invalid;
        memcpy(prefix,p,strlen(p)+1);memcpy(stem,slash+1,length+1);
        next_frame=minimum;enabled=1;
    }
    if(!enabled||!device||attempts>=CROWD_SNAPSHOTS||frame<minimum||frame<=last_frame)return;
    last_frame=frame;
    uint32_t hair_flip=0;
    if(match_hair) {
        if(hair_queries+queries>=CROWD_QUERIES)goto exhausted;
        hair_queries++;
        if(!nv2a_pb_hair_capture_completed_frame(&hair_flip))return;
    } else if(frame<next_frame)return;
    if(hair_queries+queries>=CROWD_QUERIES)goto exhausted;
    queries++;next_frame=frame+interval;
    uint32_t addresses[CROWD_ATLASES],total=0;
    if(d3d8_vk_rt_diagnostic_rgb565(addresses,CROWD_ATLASES,&total)!=1||!total)return;
    unsigned count=total<CROWD_ATLASES?total:CROWD_ATLASES;
    attempts++; /* Failed reads/files never create an unbounded retry loop. */
    char name[256],path[1024],image_name[256];
    snprintf(image_name,sizeof image_name,"%s-crowd-snapshot%02u-frame%08llu.bmp",stem,attempts,(unsigned long long)frame);
    snprintf(path,sizeof path,"%s-crowd-snapshot%02u-frame%08llu.bmp",prefix,attempts,(unsigned long long)frame);
    D3DSURFACE_DESC desc={0};HRESULT result=0;int image_ok=0;
    uint64_t remaining=CROWD_BMP_BUDGET-bmp_written;
    if(remaining>54)image_ok=capture_backbuffer_limited(device,path,&desc,&result,1,
        remaining<CROWD_BMP_SINGLE?remaining:CROWD_BMP_SINGLE);
    if(image_ok)bmp_written+=(uint64_t)desc.Width*desc.Height*4u+54u;
    struct CrowdImage {uint32_t address;int readback,saved;uint64_t hash;D3D8VkRasterExtent extent;char name[256];} rows[CROWD_ATLASES];
    memset(rows,0,sizeof rows);unsigned saved=0;
    unsigned char *rgba=malloc(CROWD_PIXELS);
    for(unsigned i=0;i<count;i++) {
        rows[i].address=addresses[i];
        if(!rgba||CROWD_RAW_BUDGET-raw_written<CROWD_PIXELS){rows[i].readback=-2;continue;}
        if(!d3d8_vk_rt_extent(addresses[i],&rows[i].extent)||
           rows[i].extent.guest_width!=64||rows[i].extent.guest_height!=128){rows[i].readback=-3;continue;}
        rows[i].readback=d3d8_vk_rt_readback(addresses[i],rgba,64*4);
        if(rows[i].readback!=1)continue;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(size_t j=0;j<CROWD_PIXELS;j++){hash^=rgba[j];hash*=UINT64_C(1099511628211);}
        rows[i].hash=hash;
        snprintf(rows[i].name,sizeof rows[i].name,"%s-crowd-snapshot%02u-frame%08llu-rt%08X.rgba",stem,attempts,(unsigned long long)frame,addresses[i]);
        snprintf(path,sizeof path,"%s-crowd-snapshot%02u-frame%08llu-rt%08X.rgba",prefix,attempts,(unsigned long long)frame,addresses[i]);
        rows[i].saved=crowd_write(path,rgba,CROWD_PIXELS);
        if(rows[i].saved){raw_written+=CROWD_PIXELS;saved++;}
    }
    free(rgba);
    snprintf(name,sizeof name,"%s-crowd-snapshot%02u-frame%08llu.json",stem,attempts,(unsigned long long)frame);
    snprintf(path,sizeof path,"%s-crowd-snapshot%02u-frame%08llu.json",prefix,attempts,(unsigned long long)frame);
    FILE *metadata=crowd_exclusive(path);int metadata_ok=metadata!=NULL;
    if(metadata) {
        if(fprintf(metadata,"{\"author\":\"djultra64\",\"host_frame\":%llu,\"elapsed_ns\":%llu,\"hair_payload_flip\":%u,\"snapshot\":%u,\"total_live_atlases\":%u,\"enumerated_atlases\":%u,\"saved_atlases\":%u,\"complete\":%s,\"backbuffer_saved\":%s,\"backbuffer\":\"%s\",\"backbuffer_width\":%u,\"backbuffer_height\":%u,\"atlas_width\":64,\"atlas_height\":128,\"layout\":\"RGBA8\",\"source\":\"live_GPU_RT_readback\",\"atlases\":[",
           (unsigned long long)frame,(unsigned long long)elapsed_ns,hair_flip,attempts,total,count,saved,
           saved==total&&image_ok?"true":"false",image_ok?"true":"false",image_name,desc.Width,desc.Height)<0)metadata_ok=0;
        for(unsigned i=0;i<count;i++)if(fprintf(metadata,"%s{\"address\":\"%08X\",\"readback\":%d,\"saved\":%s,\"bytes\":%u,\"raster_width\":%u,\"raster_height\":%u,\"fnv1a64\":\"%016llx\",\"file\":\"%s\"}",
            i?",":"",rows[i].address,rows[i].readback,rows[i].saved?"true":"false",rows[i].saved?CROWD_PIXELS:0,rows[i].extent.raster_width,rows[i].extent.raster_height,(unsigned long long)rows[i].hash,rows[i].name)<0)metadata_ok=0;
        if(fputs("]}\n",metadata)==EOF)metadata_ok=0;
        if(fclose(metadata))metadata_ok=0;
        if(!metadata_ok)unlink(path);
    }
    fprintf(stderr,"[CROWD-ATLAS-CAPTURE] snapshot=%u host_frame=%llu hair_flip=%u live=%u retained=%u saved=%u truncated=%u backbuffer=%d metadata=%d atlas_bytes=%llu bmp_bytes=%llu queries=%u\n",
        attempts,(unsigned long long)frame,hair_flip,total,count,saved,total>count,image_ok,metadata_ok,
        (unsigned long long)raw_written,(unsigned long long)bmp_written,queries);
    extern void nv2a_pb_dump_present_history(uint64_t,uint64_t);
    nv2a_pb_dump_present_history(frame,elapsed_ns);
    return;
exhausted:
    if(!exhausted_reported++){fputs("[CROWD-ATLAS-CAPTURE] bounded query budget exhausted; disabled\n",stderr);}
    return;
invalid:
    fputs("[CROWD-ATLAS-CAPTURE] invalid/missing private prefix, configuration or backend API; disabled\n",stderr);
}
#else
static void capture_crowd_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
{ (void)device;(void)elapsed_ns;(void)frame; }
#endif

/* Diagnostic only: the API copies the live GPU target, never guest RAM. */
static void capture_rt(const char *prefix,uint64_t frame,uint32_t address)
{
    D3D8VkRasterExtent extent={0};
    if(!d3d8_vk_rt_extent(address,&extent) || !extent.guest_width || !extent.guest_height ||
       extent.guest_width>2048 || extent.guest_height>2048){
        fprintf(stderr,"[FRAME-CAPTURE-RT] FAILED address=%08X frame=%llu invalid/missing extent %ux%u\n",address,(unsigned long long)frame,extent.guest_width,extent.guest_height);return;
    }
    size_t bytes=(size_t)extent.guest_width*extent.guest_height*4u;
    unsigned char *rgba=malloc(bytes);
    if(!rgba){fputs("[FRAME-CAPTURE-RT] FAILED allocation\n",stderr);return;}
    int result=d3d8_vk_rt_readback(address,rgba,extent.guest_width*4u);
    if(result!=1){fprintf(stderr,"[FRAME-CAPTURE-RT] FAILED address=%08X frame=%llu readback=%d\n",address,(unsigned long long)frame,result);free(rgba);return;}
    uint64_t hash=UINT64_C(14695981039346656037);
    uint32_t alpha[256]={0},color[3][16]={{0}};
    for(size_t i=0;i<bytes;i++){hash^=rgba[i];hash*=UINT64_C(1099511628211);}
    for(size_t i=0;i<bytes;i+=4){alpha[rgba[i+3]]++;for(unsigned c=0;c<3;c++)color[c][rgba[i+c]>>4]++;}
    char raw[1024],meta[1024],temp[1100];int ok=1;
    snprintf(raw,sizeof raw,"%s-frame%08llu-rt%08X.rgba",prefix,(unsigned long long)frame,address);
    snprintf(meta,sizeof meta,"%s-frame%08llu-rt%08X.json",prefix,(unsigned long long)frame,address);
    snprintf(temp,sizeof temp,"%s.tmp",raw);
    FILE *f=fopen(temp,"wb");
    if(!f)ok=0;else{if(fwrite(rgba,1,bytes,f)!=bytes)ok=0;if(fclose(f))ok=0;}
    if(ok && rename(temp,raw))ok=0;
    if(!ok)remove(temp);
    if(ok){
        snprintf(temp,sizeof temp,"%s.tmp",meta);f=fopen(temp,"wb");
        if(!f)ok=0;else{
            if(fprintf(f,"{\"address\":\"%08X\",\"frame\":%llu,\"width\":%u,\"height\":%u,\"raster_width\":%u,\"raster_height\":%u,\"bytes\":%zu,\"layout\":\"RGBA8\",\"fnv1a64\":\"%016llx\",\"alpha_histogram\":[",address,(unsigned long long)frame,extent.guest_width,extent.guest_height,extent.raster_width,extent.raster_height,bytes,(unsigned long long)hash)<0)ok=0;
            for(unsigned i=0;i<256;i++)if(fprintf(f,"%s%u",i?",":"",alpha[i])<0)ok=0;
            if(fputs("],\"rgb_histogram_16bins\":[",f)==EOF)ok=0;
            for(unsigned c=0;c<3;c++){
                if(fputs(c?",[":"[",f)==EOF)ok=0;
                for(unsigned i=0;i<16;i++)if(fprintf(f,"%s%u",i?",":"",color[c][i])<0)ok=0;
                if(fputc(']',f)==EOF)ok=0;
            }
            if(fputs("]}\n",f)==EOF)ok=0;
            if(fclose(f))ok=0;
        }
        if(ok && rename(temp,meta))ok=0;
        if(!ok){remove(temp);remove(raw);}
    }
    fprintf(stderr,"[FRAME-CAPTURE-RT] %s address=%08X frame=%llu size=%ux%u bytes=%zu hash=%016llx\n",ok?"saved":"FAILED write",address,(unsigned long long)frame,extent.guest_width,extent.guest_height,bytes,(unsigned long long)hash);
    free(rgba);
}

void topspin_frame_capture_after_present(IDirect3DDevice8 *device,
                                         uint64_t elapsed_ns,uint64_t frame)
{
    capture_replay_after_present(device,elapsed_ns,frame);
    capture_request_after_present(device,elapsed_ns,frame);
    capture_crowd_after_present(device,elapsed_ns,frame);
    static int initialized,enabled,frames_mode;
    static char prefix[768];
    static uint64_t targets[16];
    static unsigned count,next;
    static uint32_t rt_address;static int rt_enabled;
    if(!initialized){
        const char *p=getenv("TOPSPIN_CAPTURE_PREFIX"),*s=getenv("TOPSPIN_CAPTURE_SECONDS");
        initialized=1;
        if(!p || !*p)return;
        if(strlen(p)>=sizeof prefix){fputs("[FRAME-CAPTURE] prefix too long; disabled\n",stderr);return;}
        memcpy(prefix,p,strlen(p)+1);
        const char *frames=getenv("TOPSPIN_CAPTURE_FRAMES");
        if(frames){
            frames_mode=1;s=frames;
            while(*s && count<16){
                uint64_t value=0;
                if(*s<'0'||*s>'9')break;
                do{
                    unsigned digit=(unsigned)(*s-'0');
                    if(value>(10000000u-digit)/10u){
                        fputs("[FRAME-CAPTURE] invalid TOPSPIN_CAPTURE_FRAMES; disabled\n",stderr);return;
                    }
                    value=value*10u+digit;s++;
                }while(*s>='0'&&*s<='9');
                if(!value || (*s && *s!=',')){
                    fputs("[FRAME-CAPTURE] invalid TOPSPIN_CAPTURE_FRAMES; disabled\n",stderr);return;
                }
                targets[count++]=value;
                if(*s){s++;if(!*s){fputs("[FRAME-CAPTURE] trailing comma in frames; disabled\n",stderr);return;}}
            }
            if(*s||!count){fputs("[FRAME-CAPTURE] frames must contain1..16 integers in1..10000000; disabled\n",stderr);return;}
        }else{
        if(!s)s="25,50,80";
        while(*s && count<16){
            char *end;
            errno=0;double seconds=strtod(s,&end);
            if(end==s || errno || !isfinite(seconds) || seconds<0 ||
               seconds>=(double)UINT64_MAX/1e9 || (*end && *end!=',')){
                fputs("[FRAME-CAPTURE] invalid TOPSPIN_CAPTURE_SECONDS; disabled\n",stderr);return;
            }
            targets[count++]=(uint64_t)(seconds*1e9);
            s=*end?end+1:end;
            if(*end && !*s){fputs("[FRAME-CAPTURE] trailing comma in schedule; disabled\n",stderr);return;}
        }
        if(*s || !count){fputs("[FRAME-CAPTURE] schedule must contain 1..16 times; disabled\n",stderr);return;}
        }
        for(unsigned i=1;i<count;i++){
            uint64_t t=targets[i];unsigned j=i;
            while(j && targets[j-1]>t){targets[j]=targets[j-1];j--;}
            targets[j]=t;
        }
        unsigned unique=0;
        for(unsigned i=0;i<count;i++)if(!unique || targets[i]!=targets[unique-1])targets[unique++]=targets[i];
        count=unique;enabled=1;
        const char *rt=getenv("TOPSPIN_CAPTURE_RT_ADDRESS");
        if(rt){
            int valid=strlen(rt)==8;uint32_t value=0;
            for(unsigned i=0;valid && i<8;i++){
                unsigned c=(unsigned char)rt[i],digit;
                if(c>='0'&&c<='9')digit=c-'0';
                else if(c>='a'&&c<='f')digit=c-'a'+10;
                else if(c>='A'&&c<='F')digit=c-'A'+10;
                else{valid=0;break;}
                value=(value<<4)|digit;
            }
            if(valid){rt_address=value;rt_enabled=1;}
            else fputs("[FRAME-CAPTURE-RT] invalid address: expected exactly8 hex digits; RT capture disabled\n",stderr);
        }
    }
    if(!enabled || !device || next>=count)return;
    if(frames_mode){
        while(next<count && targets[next]<frame){
            fprintf(stderr,"[FRAME-CAPTURE] missed requested_frame=%llu actual_frame=%llu\n",
                    (unsigned long long)targets[next++],(unsigned long long)frame);
        }
        if(next>=count || frame!=targets[next])return;
    }else if(elapsed_ns<targets[next])return;
    /* One attempt per scheduled instant, no repeated disk/API failure loop. */
    const uint64_t wanted=targets[next++];
    char path[1024];
    if(frames_mode)snprintf(path,sizeof path,"%s-frame%08llu.bmp",prefix,(unsigned long long)wanted);
    else snprintf(path,sizeof path,"%s-%06llums.bmp",prefix,(unsigned long long)(wanted/1000000));
    D3DSURFACE_DESC desc={0};HRESULT result=0;
    int ok=capture_backbuffer(device,path,&desc,&result,0);
    if(frames_mode)
        fprintf(stderr,"[FRAME-CAPTURE] %s %s requested_frame=%llu actual=%.3fs frame=%llu size=%ux%u format=%08X api=%08X\n",
                ok?"saved":"FAILED",path,(unsigned long long)wanted,(double)elapsed_ns/1e9,
                (unsigned long long)frame,desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)result);
    else fprintf(stderr,"[FRAME-CAPTURE] %s %s requested=%.3fs actual=%.3fs frame=%llu size=%ux%u format=%08X api=%08X\n",
            ok?"saved":"FAILED",path,(double)wanted/1e9,(double)elapsed_ns/1e9,
            (unsigned long long)frame,desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)result);
    if(rt_enabled)capture_rt(prefix,frame,rt_address);
    /* Pair bounded screenshots with the exact completed PB frame. This is
     * diagnostic IO only; leave disabled during performance measurements. */
    const char *history=getenv("TOPSPIN_CAPTURE_PB_HISTORY");
    if(history && !strcmp(history,"1")){
        extern void nv2a_pb_dump_present_history(uint64_t,uint64_t);
        nv2a_pb_dump_present_history(frame,elapsed_ns);
    }
}
