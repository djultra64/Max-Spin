/* One opt-in source snapshot at the original DXT encoder CPU lock.
 * Diagnostic only: no guest/image writes, color conversion or render changes.
 * Own work: djultra64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "playercreator_material_read_trace.h"
#include "material_source_lock.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <fenv.h>
#include <limits.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
extern _Thread_local uint32_t g_itail_site;
extern uint32_t xbox_ContiguousAllocatedBytes(void);
extern void sub_0015E7D0_gen(void);
typedef struct {uint32_t r[12];int df,top,cmp;uint16_t cw,cc;double fp[8];RecompXmm x[8];RecompMmx m[8];fenv_t host;} Cpu;
static void cpu_save(Cpu*s){
 uint32_t r[]={g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,g_seh_ebp,g_fs_base,g_itail_site,0};memcpy(s->r,r,sizeof r);
 s->df=g_df;s->top=g_fp_top;s->cmp=g_fp_cmp;s->cw=g_fp_control_word;s->cc=g_fp_cc;memcpy(s->fp,g_fp_stack,sizeof s->fp);
 RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};memcpy(s->x,x,sizeof x);
 RecompMmx m[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};memcpy(s->m,m,sizeof m);fegetenv(&s->host);
}
static void cpu_restore(const Cpu*s){
 g_eax=s->r[0];g_ecx=s->r[1];g_edx=s->r[2];g_ebx=s->r[3];g_esi=s->r[4];g_edi=s->r[5];g_ebp=s->r[6];g_esp=s->r[7];g_seh_ebp=s->r[8];g_fs_base=s->r[9];g_itail_site=s->r[10];
 g_df=s->df;g_fp_top=s->top;g_fp_cmp=s->cmp;g_fp_control_word=s->cw;g_fp_cc=s->cc;memcpy(g_fp_stack,s->fp,sizeof s->fp);
 g_xmm0=s->x[0];g_xmm1=s->x[1];g_xmm2=s->x[2];g_xmm3=s->x[3];g_xmm4=s->x[4];g_xmm5=s->x[5];g_xmm6=s->x[6];g_xmm7=s->x[7];
 g_mm0=s->m[0];g_mm1=s->m[1];g_mm2=s->m[2];g_mm3=s->m[3];g_mm4=s->m[4];g_mm5=s->m[5];g_mm6=s->m[6];g_mm7=s->m[7];fesetenv(&s->host);
}

#define BUDGET (512u*512u*4u)
static struct {int enabled;atomic_int attempted;char prefix[PATH_MAX];unsigned char*cpu;TopspinMaterialReadSnapshot*snapshot;TopspinMaterialCapture capture;} trace;
static int low_range(uint32_t a,size_t n){return a>=0x10000u&&(uint64_t)a+n<=0x4000000u;}
static void*low(uint32_t a,size_t n){return low_range(a,n)?(void*)XBOX_PTR(a):NULL;}
static uint32_t word(uint32_t a){uint32_t x=0;void*p=low(a,4);if(p)memcpy(&x,p,4);return x;}
static uint32_t resolve(uint32_t a,uint32_t allocated){return a<allocated?0x80000000u+a:a;}
static void*pixel_pointer(uint32_t a,size_t n,uint32_t allocated){
 if(low_range(a,n))return(void*)XBOX_PTR(a);
 if(a>=0x80000000u&&(uint64_t)a+n<=0x84000000ull&&(uint64_t)a+n<=0x80000000ull+allocated)return(void*)XBOX_PTR(a);
 return NULL;
}
static uint64_t hash(const void*p,size_t n){const unsigned char*b=p;uint64_t h=14695981039346656037ull;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
static int write_bytes(const char*suffix,const void*data,size_t n){
 char path[PATH_MAX];int k=snprintf(path,sizeof path,"%s%s",trace.prefix,suffix);if(k<0||(size_t)k>=sizeof path)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 const unsigned char*p=data;size_t at=0;int ok=1;while(at<n){ssize_t z=write(fd,p+at,n-at);if(z<=0){ok=0;break;}at+=(size_t)z;}
 if(close(fd))ok=0;
 if(!ok)unlink(path);
 return ok;
}
int topspin_creator_material_read_configure(TopspinMaterialCapture capture){
 const char*on=getenv("TOPSPIN_CREATOR_MATERIAL_READ_TRACE");if(!on||strcmp(on,"1"))return 1;
 const char*prefix=getenv("TOPSPIN_CREATOR_MATERIAL_READ_PREFIX");if(trace.enabled||!capture||!prefix||prefix[0]!='/'||strlen(prefix)+32>=sizeof trace.prefix)return 0;
 trace.cpu=malloc(BUDGET);trace.snapshot=calloc(1,sizeof*trace.snapshot);if(!trace.cpu||!trace.snapshot){free(trace.cpu);free(trace.snapshot);trace.cpu=NULL;trace.snapshot=NULL;return 0;}
 strcpy(trace.prefix,prefix);trace.capture=capture;trace.enabled=1;return 1;
}
void sub_0015E7D0(void){
 McSourceLock coherence;mc_source_lock_begin(&coherence);
 uint32_t entry=0,resource=0,lr=0,header[5]={0},desc[7]={0};int candidate=0;
 if(trace.enabled&&!atomic_load_explicit(&trace.attempted,memory_order_relaxed)){
  entry=g_esp;
  /* Original162310 pushes six args before calling this lock. Its caller is
   * therefore entry+1C. Only7B910's FIRST/source lock7B93C qualifies. */
  candidate=low(entry,0xdc)&&word(entry)==0x16232du&&word(entry+0x1c)==0x7b93cu&&word(entry+8)==0&&word(entry+12)==0&&word(entry+20)==0&&word(entry+24)==0;
  if(candidate){resource=word(entry+4);lr=word(entry+16);candidate=low(resource,sizeof header)&&low(lr,8);if(candidate){memcpy(header,low(resource,sizeof header),sizeof header);memcpy(desc,low(entry+0xc0,sizeof desc),sizeof desc);candidate=((header[3]>>8)&255)==6&&desc[0]==6&&desc[5]>0&&desc[5]<=512&&desc[6]>0&&desc[6]<=512;}}
 }
 sub_0015E7D0_gen();
 mc_source_lock_end(&coherence);
 if(!candidate||g_esp!=entry+28)return;
 uint32_t pitch=word(lr),bits=word(lr+4),w=desc[5],h=desc[6];size_t bytes=(size_t)w*h*4;uint32_t allocated=xbox_ContiguousAllocatedBytes();
 void*pixels=pixel_pointer(bits,bytes,allocated);
 if(!pixels||bytes>BUDGET||atomic_exchange_explicit(&trace.attempted,1,memory_order_acq_rel))return;
 Cpu saved;cpu_save(&saved);memcpy(trace.cpu,pixels,bytes);
 TopspinMaterialReadRequest request={{resolve(header[1],allocated),bits},w,h};
 int capture_status=trace.capture(&request,trace.snapshot);
 uint32_t*addresses=request.addresses;uint32_t*widths=trace.snapshot->widths,*heights=trace.snapshot->heights;int*known=trace.snapshot->known;
 int selected=capture_status==1?trace.snapshot->selected:-1,gpu_result=capture_status==1?trace.snapshot->gpu_readback:0;
 int cpu_written=write_bytes(".cpu.bin",trace.cpu,bytes),gpu_written=gpu_result==1&&write_bytes(".gpu.rgba",trace.snapshot->rgba,bytes);
 char json[2048];int n=snprintf(json,sizeof json,
  "{\n \"author\":\"djultra64\",\n \"source_caller\":%u,\n \"resource\":%u,\n \"header\":[%u,%u,%u,%u,%u],\n \"source_descriptor\":[%u,%u,%u,%u,%u,%u,%u],\n \"width\":%u,\n \"height\":%u,\n \"returned_pitch\":%u,\n \"returned_bits\":%u,\n \"contiguous_allocated\":%u,\n \"query_addresses\":[%u,%u],\n \"query_known\":[%d,%d],\n \"query_widths\":[%u,%u],\n \"query_heights\":[%u,%u],\n \"selected\":%d,\n \"capture_status\":%d,\n \"allocation_generation_available\":%u,\n \"cpu_bytes\":%zu,\n \"cpu_raw_fnv64\":\"%016llx\",\n \"cpu_written\":%d,\n \"gpu_readback\":%d,\n \"gpu_written\":%d,\n \"gpu_rgba_fnv64\":\"%016llx\",\n \"layout_note\":\"CPU raw guest source; original7AF60 unswizzles through1B1397 before7B130. GPU dump is host RGBA. No conversion or guest writes.\"\n}\n",
  0x7b93cu,resource,header[0],header[1],header[2],header[3],header[4],desc[0],desc[1],desc[2],desc[3],desc[4],desc[5],desc[6],w,h,pitch,bits,allocated,addresses[0],addresses[1],known[0],known[1],widths[0],widths[1],heights[0],heights[1],selected,capture_status,trace.snapshot->allocation_generation_available,bytes,(unsigned long long)hash(trace.cpu,bytes),cpu_written,gpu_result,gpu_written,(unsigned long long)(gpu_result==1?hash(trace.snapshot->rgba,bytes):0));
 if(n>0&&(size_t)n<sizeof json)write_bytes(".json",json,(size_t)n);
 fprintf(stderr,"[CREATOR-MATERIAL-READ] source7B93C resource=%08X data=%08X pBits=%08X pitch=%u dimensions=%ux%u RTknown=%d/%d selected=%d GPUread=%d CPUfile=%d GPUfile=%d; no guest writes\n",resource,header[1],bits,pitch,w,h,known[0],known[1],selected,gpu_result,cpu_written,gpu_written);
 free(trace.cpu);free(trace.snapshot);trace.cpu=NULL;trace.snapshot=NULL;cpu_restore(&saved);
}
