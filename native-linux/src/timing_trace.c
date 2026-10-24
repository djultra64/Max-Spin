/* Bounded, opt-in timing observations. Own work: djultra64. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "timing_trace.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <signal.h>

#define ROW_CAP 4096u
#define PHASE_CAP 16u
/* One writer owns a row under gate. Try-lock failure is recorded as data loss,
 * never a wait while holding the guest CPU token. File IO belongs only to poll. */
typedef struct Row {
    TopspinTimingSample timer;
    uint64_t wall_ns, host_frame, guest_swaps, vblanks, timer_samples;
    uint64_t phase_enter, phase_exit;
    uint64_t host_thread_slot, thread_cpu_ns;
    uint32_t pc, guest_thread, context;
    unsigned kind; /* 1 phase-enter, 2 phase-exit, 3 timer, 4 present */
} Row;
typedef struct Phase { uint32_t pc; uint64_t enter, exit; } Phase;
static Row rows[ROW_CAP];
static Phase phases[PHASE_CAP];
static unsigned used, phase_used;
static uint64_t start_ns, end_ns, timer_samples;
static uint64_t last_frame, last_swaps = UINT64_MAX, last_vblanks = UINT64_MAX;
static TopspinTimingSample latest;
static char path[4096];
static atomic_int enabled, finished;
static atomic_uint_fast64_t lost;
static atomic_flag gate = ATOMIC_FLAG_INIT;
static int written;
/* Optional existing gperftools library. Lifecycle/file IO runs only in poll,
 * on the host main thread, never from a guest/GPU producer callback. */
static char profile_path[4096];
static int (*profile_start)(const char *);
static void (*profile_stop)(void);
static int profile_active,profile_done;
static int profile_init(void)
{
    const char *p=getenv("TOPSPIN_CPU_PROFILE");
    if(!p)return 1;
    if(!*p || strlen(p)>=sizeof profile_path || !getenv("TOPSPIN_TIMING_TRACE") ||
       getenv("CPUPROFILE") || getenv("CPUPROFILESIGNAL") || getenv("CPUPROFILE_REALTIME")) {
        fputs("[CPU-PROFILE] requires timing trace and no automatic/realtime profiler environment\n",stderr);return 0;
    }
#ifndef _WIN32
    struct sigaction action;
    if(sigaction(SIGPROF,NULL,&action) || action.sa_handler!=SIG_DFL) {
        fputs("[CPU-PROFILE] SIGPROF already in use; profiling not installed\n",stderr);return 0;
    }
    void *library=dlopen("libprofiler.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"[CPU-PROFILE] libprofiler unavailable: %s\n",dlerror());return 0;}
    profile_start=(int(*)(const char*))dlsym(library,"ProfilerStart");
    profile_stop=(void(*)(void))dlsym(library,"ProfilerStop");
    if(!profile_start || !profile_stop){fputs("[CPU-PROFILE] required profiler APIs missing\n",stderr);return 0;}
    memcpy(profile_path,p,strlen(p)+1);
    /* Keep the library loaded through process exit: it owns its SIGPROF handler. */
#else
    fputs("[CPU-PROFILE] gperftools profiling is unavailable on native Windows\n",stderr);return 0;
#endif
    return 1;
}
static int profile_poll(uint64_t now,int force)
{
    if(!profile_start || profile_done)return 1;
    if(profile_active && (force || now>=end_ns || atomic_load(&finished))) {
        profile_stop();profile_active=0;profile_done=1;
        fprintf(stderr,"[CPU-PROFILE] stopped at %" PRIu64 " ns: %s\n",now,profile_path);
    } else if(!profile_active && !force && now>=start_ns && now<end_ns && !atomic_load(&finished)) {
        if(!profile_start(profile_path)){fputs("[CPU-PROFILE] start failed\n",stderr);profile_done=1;return 0;}
        profile_active=1;
        fprintf(stderr,"[CPU-PROFILE] started at %" PRIu64 " ns: %s\n",now,profile_path);
    } else if(!profile_active && (force || now>=end_ns)) {
        profile_done=1;fputs("[CPU-PROFILE] window missed or shutdown before start; no profile\n",stderr);
    }
    return 1;
}
static atomic_uint_fast64_t next_host_thread_slot;
static _Thread_local uint64_t host_thread_slot;
/* Called only after the opt-in window/gate accepts a row. Unlike wall time,
 * this measures all CPU consumed by this host thread between observations,
 * including guest and host work outside the narrow dispatch wrappers.
 * Slot IDs are process-local, never OS IDs or guest thread IDs. */
static void thread_clock(Row *r)
{
    if (!host_thread_slot)
        host_thread_slot=atomic_fetch_add(&next_host_thread_slot,1)+1;
    r->host_thread_slot=host_thread_slot;
    r->thread_cpu_ns=UINT64_MAX;
#ifdef CLOCK_THREAD_CPUTIME_ID
    struct timespec t;
    if (!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))
        r->thread_cpu_ns=(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
#endif
}

static int seconds(const char *text, uint64_t fallback, uint64_t *out)
{
    uint64_t n = 0;
    if (!text) { *out = fallback * 1000000000u; return 1; }
    if (!*text) return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || n > 86400u) return 0;
        n = n * 10u + (unsigned)(*text - '0');
    }
    if (n > 86400u) return 0;
    *out = n * 1000000000u;
    return 1;
}
int topspin_timing_trace_init(void)
{
    const char *p = getenv("TOPSPIN_TIMING_TRACE");
    uint64_t duration;
    if (!p) {
        if(getenv("TOPSPIN_CPU_PROFILE")){fputs("[CPU-PROFILE] requires TOPSPIN_TIMING_TRACE\n",stderr);return 0;}
        return 1;
    }
    if (!*p || strlen(p) >= sizeof(path) ||
        !seconds(getenv("TOPSPIN_TIMING_START_SECONDS"), 25, &start_ns) ||
        !seconds(getenv("TOPSPIN_TIMING_DURATION_SECONDS"), 2, &duration) ||
        !duration) {
        fprintf(stderr, "[TIMING] invalid trace configuration\n");
        return 0;
    }
    end_ns = start_ns + duration;
    memcpy(path, p, strlen(p) + 1);
    if(!profile_init())return 0;
    atomic_store(&enabled, 1);
    return 1;
}
int topspin_timing_trace_enabled(void)
{ return atomic_load(&enabled) && !atomic_load(&finished); }
static int lock_record(uint64_t now)
{
    if (!atomic_load(&enabled) || atomic_load(&finished) || now < start_ns) return 0;
    if (now >= end_ns) { atomic_store(&finished, 1); return 0; }
    if (atomic_flag_test_and_set_explicit(&gate, memory_order_acquire)) {
        atomic_fetch_add(&lost, 1); return 0;
    }
    if (atomic_load(&finished)) {
        atomic_flag_clear_explicit(&gate, memory_order_release); return 0;
    }
    if (used == ROW_CAP) {
        atomic_fetch_add(&lost, 1);
        atomic_store(&finished, 1);
        atomic_flag_clear_explicit(&gate, memory_order_release); return 0;
    }
    return 1;
}
static Row *new_row(unsigned kind, uint64_t now)
{
    Row *r = &rows[used++];
    r->kind = kind; r->wall_ns = now; r->timer = latest;
    thread_clock(r);
    r->host_frame = last_frame; r->guest_swaps = last_swaps;
    r->vblanks = last_vblanks; r->timer_samples = timer_samples;
    return r;
}
static void unlock_record(void)
{
    atomic_flag_clear_explicit(&gate, memory_order_release);
}
void topspin_timing_trace_cpu_phase(uint32_t pc, int entering,
    uint32_t guest_thread, uint32_t context, uint64_t elapsed_ns)
{
    if (!lock_record(elapsed_ns)) return;
    unsigned i;
    for (i = 0; i < phase_used && phases[i].pc != pc; ++i) {}
    if (i == phase_used) {
        if (phase_used == PHASE_CAP) { atomic_fetch_add(&lost, 1); unlock_record(); return; }
        phases[phase_used++].pc = pc;
    }
    Phase *p = &phases[i];
    if (entering) ++p->enter; else ++p->exit;
    Row *r = new_row(entering ? 1u : 2u, elapsed_ns);
    r->pc = pc; r->guest_thread = guest_thread; r->context = context;
    r->phase_enter = p->enter; r->phase_exit = p->exit;
    unlock_record();
}
void topspin_timing_trace_timer_sample(const TopspinTimingSample *sample)
{
    if (!sample || !lock_record(sample->wall_ns)) return;
    latest = *sample; ++timer_samples;
    Row *r = new_row(3, sample->wall_ns);
    r->guest_thread = sample->guest_thread; r->pc = 0x24A90;
    r->context = sample->timer_va;
    unlock_record();
}
void topspin_timing_trace_present(uint64_t elapsed_ns, uint64_t host_frame,
    uint64_t guest_swaps, uint64_t vblanks)
{
    if (!lock_record(elapsed_ns)) return;
    last_frame = host_frame; last_swaps = guest_swaps; last_vblanks = vblanks;
    new_row(4, elapsed_ns);
    unlock_record();
}
int topspin_timing_trace_poll(uint64_t elapsed_ns, int force)
{
    if (!atomic_load(&enabled) || written) return 1;
    if(!profile_poll(elapsed_ns,force))return 0;
    if (force || elapsed_ns >= end_ns) atomic_store(&finished, 1);
    if (!atomic_load(&finished)) return 1;
    if (atomic_flag_test_and_set_explicit(&gate, memory_order_acquire)) return 1;
    /* Freeze before dropping the gate. All later producer calls return. */
    written = 1;
    unsigned count = used;
    uint64_t dropped = atomic_load(&lost);
    unlock_record();
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[TIMING] cannot open %s: %s\n", path, strerror(errno)); return 0; }
    fprintf(f, "event,wall_ns,guest_thread,pc,context,timer_samples,host_frames,guest_swaps,vblanks,timer_wall_ns,timer_thread,timer_va,engine_va,interval,delta_us,dt_bits,scale_bits,timer_count,override_bits,timestamp_lo,timestamp_hi,phase_enter,phase_exit,dropped,capacity,window_start_ns,window_end_ns,host_thread_slot,thread_cpu_ns\n");
    for (unsigned i = 0; i < count; ++i) {
        const Row *r = &rows[i]; const TopspinTimingSample *t = &r->timer;
        fprintf(f, "%u,%" PRIu64 ",%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
            r->kind,r->wall_ns,r->guest_thread,r->pc,r->context,r->timer_samples,r->host_frame,r->guest_swaps,r->vblanks,
            t->wall_ns,t->guest_thread,t->timer_va,t->engine_va,t->interval,t->delta_us,t->dt_bits,t->scale_bits,
            t->timer_count,t->override_bits,t->timestamp_lo,t->timestamp_hi,r->phase_enter,r->phase_exit,
            dropped,ROW_CAP,start_ns,end_ns,r->host_thread_slot,r->thread_cpu_ns);
    }
    int failed = ferror(f);
    if (fclose(f)) failed = 1;
    fprintf(stderr, "[TIMING] wrote %u rows, dropped=%" PRIu64 " to %s%s\n", count,dropped,path,failed?" (write error)":"");
    return !failed;
}
