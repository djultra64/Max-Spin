/* Original ball-state recorder; observation only. Own work: djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "physics_trace.h"
#include "gpu_preempt_timing.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#define CAP 4096u
struct Row {TopspinPhysicsSample s;uint64_t host_thread_slot,thread_cpu_ns;};
static struct Row rows[CAP];
static unsigned used;
static uint64_t start_ns,end_ns;
static char path[4096];
static atomic_int enabled,finished;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static atomic_uint_fast64_t lost,next_slot;
static _Thread_local uint64_t host_slot;
static int written;
static int gpu_preempt_enabled;
static int seconds(const char *name,unsigned fallback,unsigned *result)
{
    const char *p=getenv(name);unsigned n=0;
    if(!p){*result=fallback;return 1;}
    if(!*p)return 0;
    for(;*p;p++){if(*p<'0'||*p>'9'||n>86400u/10u)return 0;n=n*10u+(unsigned)(*p-'0');if(n>86400)return 0;}
    *result=n;return 1;
}
int topspin_physics_trace_init(void)
{
    const char *p=getenv("TOPSPIN_PHYSICS_TRACE");unsigned start,end;
    const char *gpu=getenv("TOPSPIN_GPU_PREEMPT_TIMING");
    if(gpu&&(!p||strcmp(gpu,"1"))){
        fputs("[PHYSICS] GPU hold observation requires physics trace and explicit1\n",stderr);return 0;
    }
    if(!p)return 1;
    if(!*p || strlen(p)>=sizeof path || !seconds("TOPSPIN_PHYSICS_TRACE_START_SECONDS",14,&start)
       || !seconds("TOPSPIN_PHYSICS_TRACE_END_SECONDS",40,&end) || end<=start){
        fputs("[PHYSICS] invalid path/window configuration\n",stderr);return 0;
    }
    memcpy(path,p,strlen(p)+1);start_ns=(uint64_t)start*1000000000u;end_ns=(uint64_t)end*1000000000u;
    if(gpu){if(!xbox_GpuPreemptTimingConfigure(1))return 0;gpu_preempt_enabled=1;}
    atomic_store(&enabled,1);return 1;
}
int topspin_physics_trace_enabled(void){return atomic_load_explicit(&enabled,memory_order_relaxed)&&!atomic_load_explicit(&finished,memory_order_relaxed);}
int topspin_physics_trace_wants(uint64_t now){return topspin_physics_trace_enabled()&&now>=start_ns&&now<end_ns;}
int topspin_physics_trace_gpu_preempt_enabled(void){return gpu_preempt_enabled;}
void topspin_physics_trace_sample(const TopspinPhysicsSample *s)
{
    if(!topspin_physics_trace_wants(s->wall_ns))return;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)){atomic_fetch_add(&lost,1);return;}
    if(!atomic_load(&finished)&&used<CAP){
        struct Row *r=&rows[used++];r->s=*s;
        if(!host_slot)host_slot=atomic_fetch_add(&next_slot,1)+1;
        r->host_thread_slot=host_slot;struct timespec ts;
        r->thread_cpu_ns=clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts)?UINT64_MAX:(uint64_t)ts.tv_sec*1000000000u+(uint64_t)ts.tv_nsec;
        if(used==CAP)atomic_store(&finished,1);
    }
    atomic_flag_clear_explicit(&gate,memory_order_release);
}
int topspin_physics_trace_poll(uint64_t now,int force)
{
    if(!atomic_load(&enabled)||written)return 1;
    if(!force&&now<end_ns&&!atomic_load(&finished))return 1;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire))return 1;
    atomic_store(&finished,1);unsigned count=used;
    atomic_flag_clear_explicit(&gate,memory_order_release);
    /* finished prevents new writes; no guest token or recorder gate held. */
    written=1;FILE *f=fopen(path,"w");if(!f){perror("[PHYSICS] fopen");return 0;}
    fputs("wall_ns,guest_thread,host_thread_slot,thread_cpu_ns,ball_va,ball_vtable,low_va,low_vtable,valid,ball_x_bits,ball_y_bits,ball_z_bits,ball_serial,ball_contact30,ball_contact38,ball_contact48,low_x_bits,low_y_bits,low_z_bits,velocity_x_bits,velocity_y_bits,velocity_z_bits,dt_bits,dt_next_bits,contact_active,contact_mask,interval,title_tick,title_time_bits,title_step_bits,dispatch_caller,dispatch_context,dispatch_vtable,dispatch_owner,dispatch_owner_vtable,dropped,capacity,window_start_ns,window_end_ns,gpu_hold_valid,gpu_hold_us,gpu_hold_count,gpu_hold_flags\n",f);
    for(unsigned i=0;i<count;i++){
        struct Row *r=&rows[i];TopspinPhysicsSample *s=&r->s;
        fprintf(f,"%"PRIu64",%u,%"PRIu64",%"PRIu64",%u,%u,%u,%u,%u",s->wall_ns,s->guest_thread,r->host_thread_slot,r->thread_cpu_ns,s->ball_va,s->ball_vtable,s->low_va,s->low_vtable,s->valid);
        for(unsigned j=0;j<3;j++)fprintf(f,",%u",s->ball_position[j]);
        fprintf(f,",%u",s->ball_serial);
        for(unsigned j=0;j<3;j++)fprintf(f,",%u",s->ball_contacts[j]);
        for(unsigned j=0;j<3;j++)fprintf(f,",%u",s->low_position[j]);
        for(unsigned j=0;j<3;j++)fprintf(f,",%u",s->low_velocity[j]);
        fprintf(f,",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%"PRIuFAST64",%u,%"PRIu64",%"PRIu64,s->dt_bits,s->dt_next_bits,s->contact_active,s->contact_mask,s->interval,s->title_tick,s->title_time_bits,s->title_step_bits,s->dispatch_caller,s->dispatch_context,s->dispatch_vtable,s->dispatch_owner,s->dispatch_owner_vtable,atomic_load(&lost),CAP,start_ns,end_ns);
        fprintf(f,",%u,%"PRIu64",%"PRIu64",%u\n",s->gpu_hold_valid,s->gpu_hold_us,s->gpu_hold_count,s->gpu_hold_flags);
    }
    int ok=!ferror(f);if(fclose(f))ok=0;
    fprintf(stderr,"[PHYSICS] wrote %u rows dropped=%"PRIuFAST64" to %s%s\n",count,atomic_load(&lost),path,ok?"":" (write failed)");return ok;
}
