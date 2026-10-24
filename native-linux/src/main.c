#include "sprite_ui_admission.h"
/* Top Spin Xbox native Linux probe. Own integration: djultra64.
 * SDL/Vulkan and hardware integration follows DefJamFFNY-recomp (MIT).
 * A visible window is required: there is no automatic headless fallback.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <execinfo.h>
#else
#include "host_windows.h"
#endif
#include <unistd.h>
#include "host.h"
#include "host_shutdown.h"
#include "foreign_window.h"
static int foreign_window;
static volatile sig_atomic_t parent_quit_requested;
static void request_parent_quit(int signal_number) {
    (void)signal_number;parent_quit_requested=1;
}
#include "cpu_probe.h"
#include "controls.h"
#include "frame_capture.h"
#include "black_frame_watch.h"
#include "render_config.h"
#ifdef TOPSPIN_SCENE_LIFECYCLE_OBSERVER
#include "scene_gpu_bridge.h"
#endif
#include "timing_trace.h"
#include "scheduler_lateness_trace.h"
#include "game_frame_wait.h"
#include "present_timing_csv.h"
#include "physics_trace.h"
#include "render_trace.h"
#include "career_trace.h"
#ifdef TOPSPIN_PRIVATE_APPEARANCE_OBSERVER
#include "creator_appearance_import.h"
#include "creator_input_observer.h"
#include "playercreator_material_read_trace.h"
#include "material_coherence_startup.h"
#endif
#ifdef TOPSPIN_REPLAY_SAMPLER_OBSERVER
#include "replay_sample_capture.h"
#endif
#ifdef TOPSPIN_MENU_INPUT_OBSERVER
#include "menu_input_trace.h"
#endif
#ifdef TOPSPIN_MENU_HOST
#include "menu_pc_entries.h"
#include "sprite_ui_graphics.h"
#include "menu_movie_capture.h"
static int menu_pc_enabled;
#endif
#ifdef TOPSPIN_PROMPT_IMAGE
#include "menu_prompt_runtime.h"
#endif
#include "image_handoff.h"
#ifdef _WIN32
#include "material_save_transaction.h"
#endif
#include "input/input_host.h"
#include "kernel/kernel.h"
#include "kernel/xbox_memory_layout.h"
#include "kernel/nv2a_flip.h"
#include "d3d8_xbox.h"
#include "d3d8_vk.h"
#include "d3d8_vk_rt.h"
#include "d3d8_combiners.h"
#include "nv2a_pgraph_d3d11.h"
#include "apu.h"
#include "usb/ohci.h"

typedef void (*recomp_func_t)(void);
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern MCPXAPUState *g_apu_state;
extern void xbe_entry_point(void);
extern int recomp_dispatch_init(void);
extern void xbox_PinToGuestCore(void);
extern void guest_turn_interrupts_disable(void), guest_turn_interrupts_enable(void);
extern void guest_turn_set_code(const void *,size_t);
#ifndef _WIN32
extern const char __start_guest_code[],__stop_guest_code[];
#endif
extern bool apu_hook_handle_mmio(void *, uintptr_t, uint32_t, int);
extern void ac97_bm_init(ptrdiff_t);
extern int ac97_bm_owns_address(uint32_t), ac97_bm_handle_mmio(void *, uint32_t);
extern void nv2a_regs_init(ptrdiff_t);
extern int nv2a_regs_owns_address(uint32_t), nv2a_regs_handle_mmio(void *, uint32_t);

static SDL_Window *window;
static atomic_int drawable_w = 1280, drawable_h = 720, game_done;
static const char *game_dir, *xbe_path, *save_dir;
static atomic_uint_fast64_t frames;
static atomic_int host_window_presented;
static uint32_t surface_event;
static FILE *frame_csv;
static uint64_t previous_present_ns;
static atomic_int dump_requested;
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Diagnostic signal needs lock-free atomic int");
extern void nv2a_pb_request_dump_next(void);
static void request_batch_dump(int sig) {
    (void)sig;
    atomic_store_explicit(&dump_requested,1,memory_order_relaxed);
}
typedef struct {
    void *instance, *surface;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int done, result;
} SurfaceRequest;

void host_fatal(const char *message) { fprintf(stderr, "[HOST] %s\n", message); }
/* The Vulkan backend uploads coherent host RAM through its own mapped-buffer
 * flushes. Publish guest stores here; never execute privileged host WBINVD. */
void recomp_cache_writeback(void) { atomic_thread_fence(memory_order_seq_cst); }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
void recomp_icall_fail_log(uint32_t va) {
    fprintf(stderr,"[ICALL] unresolved guest target %08x esp=%08x\n",va,g_esp);
    if(g_esp>=0x10000 && g_esp<0x04000000u)
        fprintf(stderr,"[ICALL] guest return=%08x ecx=%08x edx=%08x\n",
                *(const uint32_t*)((uintptr_t)g_xbox_mem_offset+g_esp),g_ecx,g_edx);
#ifdef _WIN32
    topspin_windows_backtrace();
#else
    void *trace[20]; int count=backtrace(trace,20);
    backtrace_symbols_fd(trace,count,STDERR_FILENO);
#endif
    _exit(126);
}
void recomp_icall_not_code_log(uint32_t va) {
    fprintf(stderr,"[ICALL] target outside guest code %08x\n",va);
    recomp_icall_fail_log(va);
}
void recomp_unimpl(const char *text, uint32_t va) {
    if(topspin_cpu_probe_instruction(text,va,(uint8_t*)g_xbox_mem_offset)) return;
    if(!strcmp(text,"cli")) { guest_turn_interrupts_disable(); return; }
    if(!strcmp(text,"sti")) { guest_turn_interrupts_enable(); return; }
    fprintf(stderr,"[UNIMPLEMENTED] guest %08x: %s; stopping instead of ignoring it\n",va,text);
    _exit(125);
}

static int handle_fault(uintptr_t host,void *context) {
    uint64_t delta = host - (uintptr_t)g_xbox_mem_offset;
    if (g_xbox_mem_offset && delta <= UINT32_MAX) {
        uint32_t va = (uint32_t)delta;
        if (va >= 0xFE800000u && va < 0xFE880000u &&
            apu_hook_handle_mmio(context,host,va,0)) return 1;
        if (xbox_OhciOwnsAddress(va) && xbox_OhciHandleMmio(context,va)) return 1;
        if (ac97_bm_owns_address(va) && ac97_bm_handle_mmio(context,va)) return 1;
        if (nv2a_regs_owns_address(va) && nv2a_regs_handle_mmio(context,va)) return 1;
    }
    fprintf(stderr,"[FAULT] host=%p guest=%08llx pc=%llx esp=%08x eax=%08x ecx=%08x edx=%08x\n",
            (void*)host,(unsigned long long)delta,(unsigned long long)HOST_PC(context),
            g_esp,g_eax,g_ecx,g_edx);
    return 0;
}
#ifdef _WIN32
static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *exception) {
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION ||
       exception->ExceptionRecord->NumberParameters<2) return EXCEPTION_CONTINUE_SEARCH;
    return handle_fault((uintptr_t)exception->ExceptionRecord->ExceptionInformation[1],exception->ContextRecord)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
#else
static void fault_handler(int sig,siginfo_t *info,void *context) {
    if(!handle_fault((uintptr_t)info->si_addr,context)) _exit(128+sig);
}
#endif

static int create_surface(void *instance, void *surface, void *unused) {
    (void)unused;
    SurfaceRequest request={.instance=instance,.surface=surface,
        .mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    SDL_Event event={0}; event.type=surface_event; event.user.data1=&request;
    pthread_mutex_lock(&request.mutex);
    if(!SDL_PushEvent(&event)) { pthread_mutex_unlock(&request.mutex); return 0; }
    while(!request.done) pthread_cond_wait(&request.condition,&request.mutex);
    pthread_mutex_unlock(&request.mutex);
    pthread_mutex_destroy(&request.mutex); pthread_cond_destroy(&request.condition);
    return request.result;
}
static void drawable_size(int *w,int *h,void *unused) {
    (void)unused; *w=atomic_load(&drawable_w); *h=atomic_load(&drawable_h);
}
static int select_render_surface(const PgraphRenderSurface *surface) {
    if(!surface) return d3d8_vk_rt_select(NULL);
    D3D8VkTarget target={surface->color,surface->zeta,surface->width,surface->height,
        surface->color_pitch,surface->zeta_pitch,surface->format,
        surface->clip_x,surface->clip_y,surface->clip_width,surface->clip_height,
        surface->extent_is_minimum};
    return d3d8_vk_rt_select(&target);
}
static int bind_texture_alias(unsigned stage,uint32_t guest_addr,uint32_t width,
                              uint32_t height,uint32_t format) {
    return d3d8_vk_rt_bind(stage,guest_addr,width,height,format);
}
static void set_polygon_offset(int enabled,float factor,float units) {
    d3d8_vk_set_polygon_offset(enabled,factor,units);
}
static void set_shadow_func(uint32_t func) {
    d3d8_combiners_set_shadow_func(func);
    static int trace=-1;
    static unsigned shown;
    if(trace<0) trace=getenv("TOPSPIN_SHADOW_TRACE")!=NULL;
    if(trace && shown++<8)
        fprintf(stderr,"[SHADOW] guest depth function=%u (raw NV097 1E6C)\n",func);
}
static void set_texture_shader_control(uint32_t dotmapping,uint32_t otherstageinput) {
    d3d8_combiners_set_texture_shader_control(dotmapping,otherstageinput);
    static int trace=-1;
    static unsigned shown;
    if(trace<0) trace=getenv("TOPSPIN_SHADOW_TRACE")!=NULL;
    if(trace && shown++<4)
        fprintf(stderr,"[TEXTURE-SHADER] guest dotmapping=%08X otherstageinput=%08X\n",
            dotmapping,otherstageinput);
}
static void set_texture_control(unsigned stage,uint32_t control,uint32_t key,int key_known) {
    d3d8_combiners_set_texture_control(stage,control,key,key_known);
}
static void select_present_surface(uint32_t color) {
    int selected=d3d8_vk_rt_present(color);
    if(selected<0) fprintf(stderr,"[PRESENT] invalid guest display allocation %08x\n",color);
    if(selected<=0) d3d8_vk_rt_present(0);
}
static uint32_t guest_swap_count(void) {
    if(strcmp(TOPSPIN_XBE_NAME,"TopSpin(Game).xbe")) return 0;
    uint32_t device=*(volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+0x1b9438u);
    if(device<0x10000u || device>0x04000000u-0x3000u) return 0;
    /* D3D_Swap increments this at 1B6400; GetSwapCount returns it at 1B647B. */
    return *(volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+device+0x2468u);
}
static uint32_t guest_present_interval(void) {
    if(strcmp(TOPSPIN_XBE_NAME,"TopSpin(Game).xbe")) return 0;
    return *(volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+0x1b939cu);
}
static void trace_miniport(uint64_t total_frames) {
    static int enabled=-1;
    if(enabled<0) enabled=getenv("TOPSPIN_MINIPORT_TRACE")!=NULL;
    if(!enabled || !total_frames || strcmp(TOPSPIN_XBE_NAME,"TopSpin(Game).xbe")) return;
    const uint32_t device=*(volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+0x1b9438u);
    if(device<0x10000u || device>0x04000000u-0x3000u) return;
    const volatile uint32_t *mini=(const volatile uint32_t*)((uintptr_t)g_xbox_mem_offset+device+0x1c18u);
    fprintf(stderr,"[MINIPORT] frames=%llu swaps=%u vblank=%u consumer=%u producer=%u "
        "slots={%u,%u,%08x},{%u,%u,%08x} PCRTC=%x/%x PMC-enable=%x\n",
        (unsigned long long)total_frames,guest_swap_count(),mini[0x1c0/4],mini[0x1bc/4],mini[0x1cc/4],
        mini[0x174/4],mini[0x178/4],mini[0x17c/4],
        mini[0x180/4],mini[0x184/4],mini[0x188/4],
        xbox_Nv2aRegRead(0xfd600100u),xbox_Nv2aRegRead(0xfd600140u),
        xbox_Nv2aRegRead(0xfd000140u));
}
static void present(void) {
    /* Execute the runtime dump request on its GPU executor, not in the signal
     * handler or SDL thread. A reference dump changes this one frame's cost. */
    if(atomic_exchange_explicit(&dump_requested,0,memory_order_relaxed))
        nv2a_pb_request_dump_next();
    IDirect3DDevice8 *device=xbox_GetD3DDevice();
    if(device) {
        HRESULT result=device->lpVtbl->Present(device,NULL,NULL,NULL,NULL);
        if(result < 0) fprintf(stderr,"[PRESENT] failed %08x\n",(unsigned)result);
        else {
            uint64_t now=SDL_GetTicksNS();
            uint64_t frame=atomic_fetch_add(&frames,1)+1;
            if(frame_csv) topspin_present_csv_row(frame_csv,frame,now,
                previous_present_ns?now-previous_present_ns:0,guest_swap_count(),guest_present_interval());
            previous_present_ns=now;
            topspin_timing_trace_present(now,frame,guest_swap_count(),UINT64_MAX);
            topspin_frame_capture_after_present(device,now,frame);
            if(topspin_black_frame_watch_after_present(device,now,frame)) {
                extern void nv2a_pb_dump_present_history(uint64_t,uint64_t);
                nv2a_pb_dump_present_history(frame,now);
                nv2a_pb_request_dump_next();
            }
        }
    }
}

/* These addresses were matched against the identical 50 original bytes in
 * each image, then translated through that image's section table. The bridge
 * still validates all 50 bytes before accepting any instruction. */
typedef struct {const char *name; uint32_t probe,device;} ImageProfile;
static const ImageProfile *image_profile(void) {
    static const ImageProfile probes[]={
        {"default.xbe",0x000b5e75,0x000674d8},
        {"TopSpin(Menu).xbe",0x001a8595,0x00166058},
        {"TopSpin(Game).xbe",0x001d3d75,0x001b9438},
        {"TopSpin(Career).xbe",0x0019130a,0x00157638},
        {"TopSpin(Training).xbe",0x001dfe95,0x001c5548},
        {"TopSpin(PlayerCreator).xbe",0x001a29f5,0x00168c68},
        {"TopSpin(Shop).xbe",0x0018d7ca,0x00153b08},
        {"TopSpin(Online).xbe",0x002a9b15,0x0026fa18},
        {"TopSpin(Stop).xbe",0x0006fe01,0x000601c8}
    };
    for(unsigned i=0;i<sizeof(probes)/sizeof(*probes);i++)
        if(!strcmp(TOPSPIN_XBE_NAME,probes[i].name)) return &probes[i];
    return NULL;
}

static void *run_guest(void *unused) {
    (void)unused;
    FILE *file=fopen(xbe_path,"rb");
    if(!file) { perror(xbe_path); atomic_store(&game_done,1); return NULL; }
    fseek(file,0,SEEK_END); long length=ftell(file); rewind(file);
    void *bytes=length>0?malloc((size_t)length):NULL;
    if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length) {
        host_fatal("Cannot read the complete XBE"); fclose(file); free(bytes);
        atomic_store(&game_done,1); return NULL;
    }
    fclose(file);
    fprintf(stderr,"[HOST] Loading %s (%ld bytes)\n",xbe_path,length);
    const ImageProfile *profile=image_profile();
    if(profile) {
        /* Three independently matched XDK fence/get-reference functions per
         * image agree on this device global and the +2C/+30 layout. See
         * tools/image_profiles.py. Register before starting the GPU worker. */
        xbox_Nv2aMirrorFence(profile->device,0x2cu,0x30u);
        /* SetFence 1AA290 and read-position wait 1AA0B0 use bits 4:0. */
        xbox_Nv2aSetFencePatternShift(0);
        const char *flip_sync=getenv("TOPSPIN_FLIP_SYNC");
        nv2a_flip_enable(!(flip_sync && !strcmp(flip_sync,"0")));
    }
    if(!xbox_MemoryLayoutInit(bytes,(size_t)length)) {
        host_fatal("Guest memory initialization failed"); free(bytes);
        atomic_store(&game_done,1); return NULL;
    }
    free(bytes);
    g_xbox_mem_offset=xbox_GetMemoryOffset();
    fprintf(stderr,"[CPU] Validated D3DX ID/MMX probe bridge at %08x: %s\n",
            profile?profile->probe:0,topspin_cpu_probe_validate_at(
                (uint8_t*)g_xbox_mem_offset,profile?profile->probe:0)?
                "enabled":"unrecognized; disabled");
    g_apu_state=mcpx_apu_init_standalone((uint8_t*)g_xbox_mem_offset);
    xbox_OhciInit();
    xbox_SetAc97PageOwner(ac97_bm_owns_address);
    ac97_bm_init(g_xbox_mem_offset);
    nv2a_regs_init(g_xbox_mem_offset);
    IDirect3D8 *factory=xbox_Direct3DCreate8(0);
    IDirect3DDevice8 *device=NULL;
    D3DPRESENT_PARAMETERS pp={0};
    pp.BackBufferWidth=1280; pp.BackBufferHeight=720; pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.BackBufferCount=1; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.Windowed=TRUE;
    if(!factory || factory->lpVtbl->CreateDevice(factory,0,1,NULL,0x40,&pp,&device)<0 || !device) {
        host_fatal("Vulkan device initialization failed"); atomic_store(&game_done,1); return NULL;
    }
    /* Wayland maps the Vulkan window on its first presentation. Show the
     * empty host surface now; this is excluded from the game frame counter. */
    if(device->lpVtbl->Present(device,NULL,NULL,NULL,NULL)>=0)
        atomic_store(&host_window_presented,1);
    pgraph_d3d11_init(); pgraph_d3d11_set_frame_end_callback(present);
    xbox_kernel_init(); xbox_path_init(game_dir,save_dir);
    if(!topspin_image_handoff_init()) {
        host_fatal("Invalid image handoff configuration");
        atomic_store(&game_done,1); return NULL;
    }
    xbox_kernel_bridge_init();
    g_esp=XBOX_STACK_TOP;
    if(!recomp_dispatch_init()) host_fatal("Flat dispatch unavailable; using generated fallback");
    xbox_WatchdogStart(); xbox_PinToGuestCore();
    fprintf(stderr,"[HOST] Entering %s; host 1280x720, title mode requires verification, timing unpatched\n",TOPSPIN_XBE_NAME);
    xbe_entry_point();
    host_fatal("Guest entry point returned"); atomic_store(&game_done,1); return NULL;
}

/* Host quit only: reuse the normal window-close shutdown/trace flush path.
 * No fabricated guest firmware call and no live Vulkan destruction. */
static void host_quit(void) {
    topspin_host_input_shutdown();
    uint64_t elapsed=SDL_GetTicksNS();
    topspin_timing_trace_poll(elapsed,1);
    if(topspin_scheduler_lateness_active()) (void)topspin_scheduler_lateness_poll(elapsed,1);
    topspin_physics_trace_poll(elapsed,1);
    topspin_render_trace_poll(elapsed,1);
    topspin_career_trace_poll();
#ifdef TOPSPIN_MENU_HOST
    topspin_menu_movie_capture_poll();
#endif
#ifdef TOPSPIN_MENU_INPUT_OBSERVER
    topspin_menu_input_trace_poll();
#endif
#ifdef TOPSPIN_REPLAY_SAMPLER_OBSERVER
    topspin_replay_sample_capture_poll(elapsed,1);
#endif
    _exit(0);
}

#ifdef _WIN32
int wmain(int argc,wchar_t **wide_argv) {
    char **argv=topspin_windows_utf8_argv(argc,wide_argv);if(!argv)return 2;
#else
int main(int argc,char **argv) {
#endif
    /* Read-only startup query for the persistent owner. Before SDL, GPU,
     * guest memory, assets or saves; stdout has one machine-readable object. */
    if(argc==2 && !strcmp(argv[1],"--query-video-size")) {
        if(!topspin_configure_render_scale()) return 2;
        unsigned width,height;topspin_render_output_size(&width,&height);
        printf("{\"width\":%u,\"height\":%u}\n",width,height);return 0;
    }
    if(argc!=4) { fprintf(stderr,"Usage: %s game-directory image.xbe save-directory\n",argv[0]); return 2; }
    game_dir=argv[1]; xbe_path=argv[2]; save_dir=argv[3];
    const char *name=strrchr(xbe_path,'/');
#ifdef _WIN32
    const char *backslash=strrchr(xbe_path,'\\');if(backslash&&(!name||backslash>name))name=backslash;
#endif
    name=name?name+1:xbe_path;
    if(strcmp(name,TOPSPIN_XBE_NAME)) {
        fprintf(stderr,"This binary requires %s; received %s\n",TOPSPIN_XBE_NAME,name);
        return 2;
    }
    setvbuf(stdout,NULL,_IOLBF,0); setvbuf(stderr,NULL,_IOLBF,0);
#ifndef _WIN32
    TopspinForeignWindow parent_window={0};char parent_error[192];
    if(!topspin_foreign_window_parse(getenv("TOPSPIN_PARENT_X11_WINDOW"),getenv("SDL_VIDEODRIVER"),
                                   getenv("SDL_VIDEO_DRIVER"),&parent_window,parent_error,sizeof parent_error)) {
        host_fatal(parent_error);return 2;
    }
    foreign_window=parent_window.enabled;
    if(foreign_window) {
        struct sigaction quit_action={0};quit_action.sa_handler=request_parent_quit;
        sigemptyset(&quit_action.sa_mask);
        if(sigaction(SIGTERM,&quit_action,NULL)||sigaction(SIGINT,&quit_action,NULL)) {
            host_fatal("Cannot install parent window shutdown signal");return 2;
        }
    }
#else
    uintptr_t parent_hwnd=topspin_windows_parent_window();
    foreign_window=parent_hwnd!=0;
#endif
#if defined(_WIN32) && !defined(TOPSPIN_PRIVATE_APPEARANCE_OBSERVER)
    const char *career=getenv("TOPSPIN_CAREER_SAVE_VERSION");
    if(career&&!strcmp(career,"1")&&!mc_save_transaction_configure(save_dir)){host_fatal("Cannot initialize private saves");return 2;}
#endif
    const char *driver_timing=getenv("TOPSPIN_DRIVER_PRESENT_TIMING");
    if(!topspin_present_csv_init(driver_timing&&!strcmp(driver_timing,"1"))) return 2;
    const char *csv_path=getenv("TOPSPIN_FRAME_CSV");
    if(csv_path && *csv_path) {
        frame_csv=fopen(csv_path,"w");
        if(!frame_csv) { perror(csv_path); return 2; }
        topspin_present_csv_header(frame_csv);
    }
#ifdef _WIN32
    if(!topspin_windows_register_guest_code())return 2;
#else
    guest_turn_set_code(__start_guest_code,(size_t)(__stop_guest_code-__start_guest_code));
#endif
    const char *defaults[]={"RECOMP_PB_EXEC","RECOMP_PB_D3D11","RECOMP_VBLANK","RECOMP_USB","RECOMP_AC97_READY","RECOMP_RGBA_TARGETS","RECOMP_GUEST_AA1"
#ifdef _WIN32
        ,"RECOMP_TITLE_KEVENTS","RECOMP_CONTIG_RECLAIM","RECOMP_GUEST_TARGETS",
        "RECOMP_NO_FENCE_CATCHUP","RECOMP_NO_FENCE_FOLLOW","RECOMP_PAD_HOST"
#endif
    };
    for(unsigned i=0;i<sizeof(defaults)/sizeof(*defaults);i++) setenv(defaults[i],"1",0);
#ifdef _WIN32
    setenv("RECOMP_WATCHDOG_SECS","0",0);
    setenv("RECOMP_RUNTIME_QUIET",getenv("TOPSPIN_MANUAL_DIAGNOSTICS")&&!strcmp(getenv("TOPSPIN_MANUAL_DIAGNOSTICS"),"1")?"0":"1",0);
#endif
#ifndef _WIN32
    struct sigaction sa={0}; sa.sa_sigaction=fault_handler; sa.sa_flags=SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask,SIGUSR2);
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);
    const char *batch_dump=getenv("RECOMP_PB_BATCH_DUMP");
    if(batch_dump && !strcmp(batch_dump,"shot")) {
        struct sigaction debug_sa={0};
        debug_sa.sa_handler=request_batch_dump; sigemptyset(&debug_sa.sa_mask);
        sigaction(SIGUSR1,&debug_sa,NULL);
    }
#else
    if(!AddVectoredExceptionHandler(1,fault_handler)){host_fatal("Cannot install device exception handler");return 2;}
#endif
    if(!topspin_configure_render_scale()) return 2;
    if(!topspin_configure_game_frame_wait(TOPSPIN_XBE_NAME)) return 2;
    if(!topspin_timing_trace_init()) return 2;
    if(getenv("TOPSPIN_SCHEDULER_TRACE")&&!topspin_scheduler_lateness_init(SDL_GetTicksNS())) return 2;
    if(!topspin_physics_trace_init()) return 2;
    if(!topspin_render_trace_init()) return 2;
#ifdef TOPSPIN_SCENE_LIFECYCLE_OBSERVER
    if(!topspin_scene_gpu_init()) {
        fputs("Scene lifecycle snapshot requires explicit lifecycle and executor configuration\n",stderr);return 2;
    }
#endif
    if(!topspin_career_trace_init()) return 2;
#ifdef TOPSPIN_PRIVATE_APPEARANCE_OBSERVER
    (void)topspin_creator_input_observer_configure();
    if(!topspin_creator_appearance_configure(save_dir)) {
        fputs("Private appearance configuration rejected; no guest startup\n",stderr);return 2;
    }
    if(!topspin_creator_material_coherence_configure(save_dir)) {
        fputs("Creator coherence configuration rejected; no guest startup\n",stderr);return 2;
    }
    if(!topspin_creator_material_read_configure(topspin_creator_material_gpu_capture)) {
        fputs("Creator material read diagnostic rejected; no guest startup\n",stderr);return 2;
    }
#endif
#ifdef TOPSPIN_REPLAY_SAMPLER_OBSERVER
    if(!topspin_replay_sample_capture_init()) return 2;
#endif
#ifdef TOPSPIN_MENU_HOST
    const char *pc_directory=getenv("TOPSPIN_MENU_PC_DIR");
    const char *graphics_ini=getenv("TOPSPIN_GRAPHICS_INI");
    if((pc_directory&&*pc_directory)||(graphics_ini&&*graphics_ini)) {
        if(!pc_directory||!*pc_directory||!graphics_ini||!*graphics_ini
           ||!topspin_menu_pc_configure(pc_directory)) {
            host_fatal("PC Menu requires valid TOPSPIN_MENU_PC_DIR and explicit TOPSPIN_GRAPHICS_INI"); return 2;
        }
        unsigned render_width,render_height;topspin_render_output_size(&render_width,&render_height);
        if(!topspin_menu_graphics_configure(graphics_ini,topspin_render_graphics_ready,NULL,
               render_height,topspin_render_antialiasing(),topspin_sprite_ui_filter_current())) {
            host_fatal("Invalid running graphics resolution");return 2;
        }
        menu_pc_enabled=1;
        fputs("[MENU-PC] native RUI graphics screen configured; preferences apply after restart\n",stderr);
    }
#endif
#ifdef TOPSPIN_PROMPT_IMAGE
    const char *sprite_cache=getenv("TOPSPIN_SPRITE_UI_CACHE");
    if(!strcmp(TOPSPIN_PROMPT_IMAGE,"menu")&&sprite_cache&&*sprite_cache
       &&!topspin_sprite_ui_initialize(TOPSPIN_PROMPT_IMAGE)) {
        host_fatal("Cannot authenticate private sprite UI cache");return 2;
    }
    char prompt_path[1024];
    const char *prompt_root=getenv("TOPSPIN_PROMPTS_ROOT");
    const char *prompt_directory=getenv("TOPSPIN_MENU_PROMPTS_DIR");
    if(prompt_root&&*prompt_root) {
        int written=snprintf(prompt_path,sizeof prompt_path,"%s/%s",prompt_root,TOPSPIN_PROMPT_IMAGE);
        if(written<0||(size_t)written>=sizeof prompt_path) {host_fatal("Controller prompt cache path too long");return 2;}
        prompt_directory=prompt_path;
    }
    if(prompt_directory && *prompt_directory) {
        if(!topspin_menu_prompts_configure_image(TOPSPIN_PROMPT_IMAGE,prompt_directory,NULL)) {
            host_fatal("Cannot initialize the requested private controller prompt resources"); return 2;
        }
        fprintf(stderr,"[RUI-PROMPTS] image=%s private resources configured; physical-family draw verification pending\n",TOPSPIN_PROMPT_IMAGE);
    }
#endif
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD)) { host_fatal(SDL_GetError()); return 1; }
    int window_w=1280,window_h=720;
    if(foreign_window) {
#ifndef _WIN32
        const char *driver=SDL_GetCurrentVideoDriver();
        if(!driver||strcmp(driver,"x11")) {host_fatal("Parent window attach requires the active SDL x11 driver");SDL_Quit();return 1;}
        SDL_PropertiesID properties=SDL_CreateProperties();
        if(!properties) {host_fatal(SDL_GetError());SDL_Quit();return 1;}
        int properties_ok=SDL_SetNumberProperty(properties,SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,(Sint64)parent_window.xid)
                         &&SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN,true);
        if(properties_ok) window=SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if(!window) {host_fatal(SDL_GetError());SDL_Quit();return 1;}
        if(!(SDL_GetWindowFlags(window)&SDL_WINDOW_EXTERNAL)
           ||SDL_GetNumberProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0)!=(Sint64)parent_window.xid
           ||!SDL_GetWindowSizeInPixels(window,&window_w,&window_h)||window_w<=0||window_h<=0) {
            host_fatal("Parent window attach did not return the requested external X11 drawable");SDL_Quit();return 1;
        }
        fprintf(stderr,"[WINDOW] attached parent X11 window=%u drawable=%dx%d; parent owns geometry/focus/lifetime\n",parent_window.xid,window_w,window_h);
#else
        if(!topspin_windows_attach_drawable(parent_hwnd,&window_w,&window_h))return 1;
        /* Parent owns HWND lifetime; never let SDL subclass it across processes. */
#endif
    } else {
        SDL_Rect usable;
        if(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),&usable)) {
            if(window_w>usable.w) {window_w=usable.w;window_h=window_w*9/16;}
            if(window_h>usable.h) {window_h=usable.h;window_w=window_h*16/9;}
        }
        window=SDL_CreateWindow("Max Spin",window_w,window_h,
                                SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
        if(!window) {host_fatal(SDL_GetError());SDL_Quit();return 1;}
        if(!SDL_SetWindowAspectRatio(window,16.0f/9.0f,16.0f/9.0f))
            fprintf(stderr,"[WINDOW] aspect constraint unavailable: %s\n",SDL_GetError());
        if(SDL_GetWindowSizeInPixels(window,&window_w,&window_h))
            fprintf(stderr,"[WINDOW] drawable %dx%d; render target 1280x720\n",window_w,window_h);
        SDL_RaiseWindow(window);
    }
    atomic_store(&drawable_w,window_w);atomic_store(&drawable_h,window_h);
    surface_event=SDL_RegisterEvents(1);
    if(surface_event==(uint32_t)-1) { host_fatal(SDL_GetError()); SDL_Quit(); return 1; }
#ifndef _WIN32
    uint32_t count=0;
    const char *const *extensions=SDL_Vulkan_GetInstanceExtensions(&count);
    if(!extensions) { host_fatal(SDL_GetError()); SDL_Quit(); return 1; }
    D3D8VkHost host={0}; host.instance_extensions=extensions; host.instance_extension_count=count;
    host.create_surface=create_surface; host.drawable_size=drawable_size;
#else
    D3D8VkHost host={0};topspin_windows_vulkan_host(&host,parent_hwnd);
    if(!foreign_window) {
        HWND hwnd=(HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
        topspin_windows_vulkan_host(&host,(uintptr_t)hwnd);
    }
#endif
    d3d8_vk_set_host(&host);
    pgraph_d3d11_set_surface_callback(select_render_surface);
    pgraph_d3d11_set_texture_alias_callback(bind_texture_alias);
    pgraph_d3d11_set_polygon_offset_callback(set_polygon_offset);
    pgraph_d3d11_set_shadow_func_callback(set_shadow_func);
    pgraph_d3d11_set_texture_shader_control_callback(set_texture_shader_control);
    pgraph_d3d11_set_texture_control_callback(set_texture_control);
    pgraph_d3d11_set_present_surface_callback(select_present_surface);
    pgraph_d3d11_set_diagnostic_readback_callback(d3d8_vk_rt_readback);
    if(sxui_available()&&!topspin_sprite_ui_register_backend()){host_fatal("Cannot configure executor-owned sprite filtering");return 2;}
    topspin_controls_start();
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,8u*1024u*1024u);
    pthread_t guest;
    if(pthread_create(&guest,&attr,run_guest,NULL)) { host_fatal("Cannot start guest thread"); return 1; }
    pthread_attr_destroy(&attr);
    uint64_t last=SDL_GetTicks(),previous=0;
    int raised=0;
    for(;;) {
#ifdef _WIN32
        if(topspin_windows_poll_input())host_quit();
#else
        if(foreign_window&&parent_quit_requested) host_quit();
#endif
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_EVENT_QUIT) {
                host_quit();
            }
            /* Original game UI owns all navigation, including Graphics Options. */
            if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP)
                topspin_controls_key(event.key.key,event.type==SDL_EVENT_KEY_DOWN);
            if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) xbox_HostInputFocus(1);
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) xbox_HostInputFocus(0);
            if(event.type==surface_event) {
                SurfaceRequest *request=event.user.data1;
                pthread_mutex_lock(&request->mutex);
                request->result=SDL_Vulkan_CreateSurface(window,(VkInstance)request->instance,NULL,
                                                         (VkSurfaceKHR*)request->surface);
                request->done=1; pthread_cond_signal(&request->condition);
                pthread_mutex_unlock(&request->mutex);
            }
            if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                atomic_store(&drawable_w,event.window.data1); atomic_store(&drawable_h,event.window.data2);
            }
        }
#ifdef TOPSPIN_MENU_HOST
        if(menu_pc_enabled) {
            TopspinMenuPcRequest request=topspin_menu_pc_take_host_request();
            if(request==TOPSPIN_MENU_PC_QUIT) host_quit();
        }
#endif
        uint64_t now=SDL_GetTicks();
        topspin_timing_trace_poll(SDL_GetTicksNS(),0);
        if(topspin_scheduler_lateness_active()) (void)topspin_scheduler_lateness_poll(SDL_GetTicksNS(),0);
        topspin_physics_trace_poll(SDL_GetTicksNS(),0);
        topspin_render_trace_poll(SDL_GetTicksNS(),0);
        topspin_career_trace_poll();
#ifdef TOPSPIN_MENU_HOST
        topspin_menu_movie_capture_poll();
#endif
#ifdef TOPSPIN_MENU_INPUT_OBSERVER
        topspin_menu_input_trace_poll();
#endif
#ifdef TOPSPIN_REPLAY_SAMPLER_OBSERVER
        topspin_replay_sample_capture_poll(SDL_GetTicksNS(),0);
#endif
        if(!foreign_window&&!raised && atomic_load(&host_window_presented)) { SDL_RaiseWindow(window); raised=1; }
        if(now-last>=2000) {
            if(frame_csv) fflush(frame_csv);
            uint64_t total=atomic_load(&frames);
            trace_miniport(total);
            fprintf(stderr,"[PRESENT] %.2f presents/s; this is NOT a gameplay benchmark until a match is verified\n",
                    (total-previous)*1000.0/(now-last));
            previous=total; last=now;
        }
        if(!foreign_window&&atomic_load(&game_done)) SDL_SetWindowTitle(window,"Max Spin has stopped");
        SDL_Delay(5);
    }
}
