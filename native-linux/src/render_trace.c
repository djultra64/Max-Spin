/* Original render/update boundary recorder; observation only. Own work: djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "render_trace.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#define CAP 4096u
struct Row {TopspinRenderSample s;uint64_t host_thread_slot;};
static struct Row rows[CAP];
static unsigned used;
static atomic_uint pending;
static uint64_t start_ns,end_ns;
static char path[4096];
static atomic_int enabled,finished;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static atomic_uint_fast64_t lost,next_slot;
static _Thread_local uint64_t host_slot;
static int written;
#define EVENT_CAP 128u
static TopspinIntervalEvent events[EVENT_CAP];
static unsigned event_used;
static char event_path[4096];
static atomic_int event_enabled,event_finished;
static atomic_uint_fast64_t event_lost;
static int event_written;
static int seconds(const char *name,unsigned fallback,unsigned *result)
{
    const char *p=getenv(name);unsigned n=0;
    if(!p){*result=fallback;return 1;}
    if(!*p)return 0;
    for(;*p;p++){if(*p<'0'||*p>'9'||n>86400u/10u)return 0;n=n*10u+(unsigned)(*p-'0');if(n>86400)return 0;}
    *result=n;return 1;
}
int topspin_render_trace_init(void)
{
    const char *p=getenv("TOPSPIN_RENDER_TRACE");
    const char *ep=getenv("TOPSPIN_INTERVAL_EVENTS");unsigned start,end;
    if(!p&&!ep)return 1;
    if((p&&(!*p || strlen(p)>=sizeof path)) || (ep&&(!*ep||strlen(ep)>=sizeof event_path)) || !seconds("TOPSPIN_RENDER_TRACE_START_SECONDS",14,&start)
       || !seconds("TOPSPIN_RENDER_TRACE_END_SECONDS",40,&end) || end<=start){
        fputs("[RENDER-TRACE] invalid path/window configuration\n",stderr);return 0;
    }
    start_ns=(uint64_t)start*1000000000u;end_ns=(uint64_t)end*1000000000u;
    if(p){memcpy(path,p,strlen(p)+1);atomic_store(&enabled,1);}
    if(ep){memcpy(event_path,ep,strlen(ep)+1);atomic_store(&event_enabled,1);}
    return 1;
}
int topspin_render_trace_enabled(void){return atomic_load_explicit(&enabled,memory_order_relaxed)&&!atomic_load_explicit(&finished,memory_order_relaxed);}
int topspin_render_trace_wants(uint64_t now){return topspin_render_trace_enabled()&&now>=start_ns&&now<end_ns;}
int topspin_render_trace_begin(uint64_t now)
{
    if(!topspin_render_trace_wants(now))return 0;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)){atomic_fetch_add(&lost,1);return 0;}
    int accept=!atomic_load(&finished);
    if(accept)atomic_fetch_add(&pending,1);
    atomic_flag_clear_explicit(&gate,memory_order_release);return accept;
}
void topspin_render_trace_sample(const TopspinRenderSample *s)
{
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)){atomic_fetch_add(&lost,1);atomic_fetch_sub(&pending,1);return;}
    if(!atomic_load(&finished)&&used<CAP){
        struct Row *r=&rows[used++];r->s=*s;
        if(!host_slot)host_slot=atomic_fetch_add(&next_slot,1)+1;
        r->host_thread_slot=host_slot;
        if(used==CAP)atomic_store(&finished,1);
    }
    else atomic_fetch_add(&lost,1);
    atomic_fetch_sub(&pending,1);
    atomic_flag_clear_explicit(&gate,memory_order_release);
}
int topspin_interval_events_enabled(void)
{return atomic_load_explicit(&event_enabled,memory_order_relaxed)&&!atomic_load_explicit(&event_finished,memory_order_relaxed);}
int topspin_interval_events_begin(uint64_t now)
{
    if(!topspin_interval_events_enabled()||now<start_ns||now>=end_ns)return 0;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)){atomic_fetch_add(&event_lost,1);return 0;}
    int accept=!atomic_load(&event_finished);
    if(accept)atomic_fetch_add(&pending,1);
    atomic_flag_clear_explicit(&gate,memory_order_release);return accept;
}
void topspin_interval_events_sample(const TopspinIntervalEvent *s)
{
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)){
        atomic_fetch_add(&event_lost,1);atomic_fetch_sub(&pending,1);return;
    }
    if(s->valid==3&&(s->before.interval!=s->after.interval||s->before.aux[21]!=s->after.aux[21])){
        if(event_used<EVENT_CAP){events[event_used++]=*s;if(event_used==EVENT_CAP)atomic_store(&event_finished,1);}
        else atomic_fetch_add(&event_lost,1);
    }
    atomic_fetch_sub(&pending,1);atomic_flag_clear_explicit(&gate,memory_order_release);
}
static int interval_events_poll(uint64_t now,int force)
{
    if(!atomic_load(&event_enabled)||event_written)return 1;
    if(!force&&now<end_ns&&!atomic_load(&event_finished))return 1;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire))return 1;
    if(atomic_load(&pending)){atomic_flag_clear_explicit(&gate,memory_order_release);return 1;}
    atomic_store(&event_finished,1);unsigned count=event_used;
    atomic_flag_clear_explicit(&gate,memory_order_release);event_written=1;
    FILE *f=fopen(event_path,"w");if(!f){perror("[INTERVAL-EVENTS] fopen");return 0;}
    fputs("wall_begin_ns,wall_end_ns,caller,context,principal,valid,event,param1,param2",f);
    const char *fields[]={"interval","aux_valid","app11c","rule11f0","replay_va","replay_vtable","replay_a90","replay_a94","replay_a98","replay_aa0","camera_va","camera_vtable","camera24_va","camera24_vtable","camera28_va","camera28_vtable","tick","time_bits","step_bits","guest_swaps"};
    for(unsigned side=0;side<2;side++)for(unsigned j=0;j<sizeof fields/sizeof fields[0];j++)fprintf(f,",%s_%s",side?"after":"before",fields[j]);
    fputs(",dropped,capacity,window_start_ns,window_end_ns\n",f);
    for(unsigned i=0;i<count;i++){
        TopspinIntervalEvent *e=&events[i];
        fprintf(f,"%"PRIu64",%"PRIu64",%u,%u,%u,%u,%u,%u,%u",e->wall_begin_ns,e->wall_end_ns,e->caller,e->context,e->principal,e->valid,e->argument[0],e->argument[1],e->argument[2]);
        for(unsigned side=0;side<2;side++){
            TopspinRenderState *v=side?&e->after:&e->before;
            uint32_t values[]={v->interval,v->aux_valid,v->aux[21],v->aux[22],v->component[9].address,v->component[9].vtable,v->aux[17],v->aux[18],v->aux[19],v->aux[20],v->component[6].address,v->component[6].vtable,v->aux[3],v->aux[23],v->aux[4],v->aux[24],v->tick,v->time_bits,v->step_bits,v->guest_swaps};
            for(unsigned j=0;j<sizeof values/sizeof values[0];j++)fprintf(f,",%u",values[j]);
        }
        fprintf(f,",%"PRIuFAST64",%u,%"PRIu64",%"PRIu64"\n",atomic_load(&event_lost),EVENT_CAP,start_ns,end_ns);
    }
    int ok=!ferror(f);if(fclose(f))ok=0;
    fprintf(stderr,"[INTERVAL-EVENTS] wrote %u rows dropped=%"PRIuFAST64" to %s%s\n",count,atomic_load(&event_lost),event_path,ok?"":" (write failed)");return ok;
}
int topspin_render_trace_poll(uint64_t now,int force)
{
    int events_ok=interval_events_poll(now,force);
    if(!atomic_load(&enabled)||written)return events_ok;
    if(!force&&now<end_ns&&!atomic_load(&finished))return events_ok;
    if(atomic_flag_test_and_set_explicit(&gate,memory_order_acquire))return 1;
    if(atomic_load(&pending)){atomic_flag_clear_explicit(&gate,memory_order_release);return 1;}
    atomic_store(&finished,1);unsigned count=used;
    atomic_flag_clear_explicit(&gate,memory_order_release);
    /* finished prevents new writes; no guest token or recorder gate held. */
    written=1;FILE *f=fopen(path,"w");if(!f){perror("[RENDER-TRACE] fopen");return 0;}
    fputs("pc,caller,context,context_vtable,guest_thread,host_thread_slot,wall_begin_ns,wall_end_ns,cpu_begin_ns,cpu_end_ns",f);
    const char *fields[]={"tick","time_bits","step_bits","interval","guest_swaps","swap_valid","ball_va","ball_vtable","low_va","low_vtable","valid","ball_serial","ball_x_bits","ball_y_bits","ball_z_bits","velocity_x_bits","velocity_y_bits","velocity_z_bits","contact_mask"};
    for(unsigned side=0;side<2;side++)for(unsigned j=0;j<sizeof fields/sizeof fields[0];j++)fprintf(f,",%s_%s",side?"after":"before",fields[j]);
    const char *component_names[]={"app2d4","app4988","app6348","app499c",
        "app6400","app6404","app6408","app640c","app6414","app6418","app6420","app63f4","renderer"};
    const char *aux_names[]={"renderer170","renderer174","renderer178_flags",
        "camera24","camera28","camera30_flags","camera1e0","camera1e4",
        "effects_b00","effects_b04","effects_b08","effects_b0c","effects_b10","effects_b14","effects_b18",
        "global2fdb28","global235474","effects_a90","effects_a94","effects_a98","effects_aa0","app11c","rule11f0","camera24_vtable","camera28_vtable"};
    for(unsigned side=0;side<2;side++){
        const char *tag=side?"after":"before";
        fprintf(f,",%s_aux_valid",tag);
        for(unsigned c=0;c<TOPSPIN_RENDER_COMPONENTS;c++){
            fprintf(f,",%s_%s_address,%s_%s_vtable,%s_%s_valid",tag,component_names[c],tag,component_names[c],tag,component_names[c]);
            for(unsigned k=0;k<9;k++)fprintf(f,",%s_%s_callback_%02x",tag,component_names[c],k*4);
        }
        for(unsigned j=0;j<TOPSPIN_RENDER_AUX_WORDS;j++)fprintf(f,",%s_%s",tag,aux_names[j]);
    }
    fputs(",dropped,capacity,window_start_ns,window_end_ns\n",f);
    for(unsigned i=0;i<count;i++){
        struct Row *r=&rows[i];TopspinRenderSample *s=&r->s;
        fprintf(f,"%u,%u,%u,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64,s->pc,s->caller,s->context,s->context_vtable,s->guest_thread,r->host_thread_slot,s->wall_begin_ns,s->wall_end_ns,s->cpu_begin_ns,s->cpu_end_ns);
        for(unsigned side=0;side<2;side++){
            TopspinRenderState *v=side?&s->after:&s->before;
            fprintf(f,",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",v->tick,v->time_bits,v->step_bits,v->interval,v->guest_swaps,v->swap_valid,v->ball_va,v->ball_vtable,v->low_va,v->low_vtable,v->valid,v->ball_serial);
            for(unsigned j=0;j<3;j++)fprintf(f,",%u",v->ball_position[j]);
            for(unsigned j=0;j<3;j++)fprintf(f,",%u",v->velocity[j]);
            fprintf(f,",%u",v->contact_mask);
        }
        for(unsigned side=0;side<2;side++){
            TopspinRenderState *v=side?&s->after:&s->before;
            fprintf(f,",%u",v->aux_valid);
            for(unsigned c=0;c<TOPSPIN_RENDER_COMPONENTS;c++){
                TopspinRenderComponent *p=&v->component[c];
                fprintf(f,",%u,%u,%u",p->address,p->vtable,p->valid);
                for(unsigned k=0;k<9;k++)fprintf(f,",%u",p->callback[k]);
            }
            for(unsigned j=0;j<TOPSPIN_RENDER_AUX_WORDS;j++)fprintf(f,",%u",v->aux[j]);
        }
        fprintf(f,",%"PRIuFAST64",%u,%"PRIu64",%"PRIu64"\n",atomic_load(&lost),CAP,start_ns,end_ns);
    }
    int ok=!ferror(f);if(fclose(f))ok=0;
    fprintf(stderr,"[RENDER-TRACE] wrote %u rows dropped=%"PRIuFAST64" to %s%s\n",count,atomic_load(&lost),path,ok?"":" (write failed)");return ok&&events_ok;
}
