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
#include <sys/stat.h>
#include <unistd.h>
#endif

static void le16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void le32(unsigned char *p,uint32_t v) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i)); }

/* This runtime's sf_LockRect explicitly returns RGBA bytes, even though
 * GetDesc names A8R8G8B8. See d3d8_vk.c's backbuffer implementation. BMP BI_RGB
 * stores BGRA; use top-down rows and preserve RGB exactly with opaque alpha. */
static int write_bmp(const char *path,const D3DSURFACE_DESC *desc,const D3DLOCKED_RECT *lock)
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
    f=fopen(temporary,"wb");
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
    if(ok && rename(temporary,path))ok=0;
    if(!ok)remove(temporary);
    return ok;
}


static int capture_backbuffer(IDirect3DDevice8 *device,const char *path,
                              D3DSURFACE_DESC *desc,HRESULT *api_result)
{
    IDirect3DSurface8 *surface=NULL;
    D3DLOCKED_RECT lock={0};
    memset(desc,0,sizeof *desc);
    int locked=0,ok=0;
    HRESULT result=device->lpVtbl->GetBackBuffer(device,0,0,&surface);
    if(result>=0 && surface){
        result=surface->lpVtbl->GetDesc(surface,desc);
        if(result>=0){
            result=surface->lpVtbl->LockRect(surface,&lock,NULL,D3DLOCK_READONLY);
            if(result>=0){locked=1;ok=write_bmp(path,desc,&lock);}
        }
        if(locked)surface->lpVtbl->UnlockRect(surface);
        surface->lpVtbl->Release(surface);
    }
    *api_result=result;return ok;
}

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
 int ok=capture_backbuffer(device,output,&desc,&result);
 fprintf(stderr,"[FRAME-CAPTURE-REQUEST] %s %s counter=%llu attempt=%u actual=%.3fs frame=%llu size=%ux%u format=%08X api=%08X\n",ok?"saved":"FAILED",output,(unsigned long long)value,attempts,(double)elapsed_ns/1e9,(unsigned long long)frame,desc.Width,desc.Height,(unsigned)desc.Format,(unsigned)result);
 return;
rejected:
 if(!(reported&(1u<<reason))){reported|=1u<<reason;fprintf(stderr,"[FRAME-CAPTURE-REQUEST] ignored invalid request reason=%u (open=1,private-file=2,transient-read=3,counter=4)\n",reason);}
}
#else
static void capture_request_after_present(IDirect3DDevice8 *device,uint64_t elapsed_ns,uint64_t frame)
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
    capture_request_after_present(device,elapsed_ns,frame);
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
    int ok=capture_backbuffer(device,path,&desc,&result);
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
