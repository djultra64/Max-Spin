#include "nv2a_executor_job.h"
#include "resource_provenance.h"
#include "resource_coherence_backend.h"
#include "resource_epoch_api.h"
#include "resource_mip_layout.h"
#include "d3d8_fog.h"
/*
 * d3d8_vk.c -- the Direct3D 8 device the push-buffer translator draws
 * through, on Vulkan.
 *
 * This is not a general Direct3D 8 implementation. A title whose own Direct3D
 * was recompiled with it never calls this interface; the translator in
 * src/nv2a/nv2a_pgraph_d3d11.c does, and it uses a small part of it:
 *
 *   - render states for depth, stencil, blending, alpha test and colour mask;
 *   - four texture stages with address and filter modes;
 *   - textures created in the title's own format and filled through LockRect;
 *   - pre-transformed vertices (XYZRHW | DIFFUSE | TEX1) by DrawPrimitiveUP;
 *   - Clear, Present, and the back buffer read back for captures;
 *   - the extras beside the interface: NV2A vertex programs
 *     (d3d8_Nv2aProgramDraw), register combiners (d3d8_combiners_set_nv2a),
 *     the surface clip, the blend colour and point sprites.
 *
 * That part is implemented here and the rest of the vtable answers without
 * doing anything.
 *
 * The shaders are the ones the Direct3D 11 backend uses: the same HLSL text,
 * from the same generators, compiled to SPIR-V at run time by shaderc. So a
 * change to how a combiner or a vertex program is translated is made once.
 * With a cache folder set (d3d8_vk_set_cache_dir), the SPIR-V and the driver's
 * pipeline cache are kept there between runs (d3d8_vk_cache.h), so a shader
 * met in an earlier run is not compiled again.
 *
 * The picture is drawn into an off-screen target `render scale` times the
 * title's 640x480 and copied to the window at present, scaled and letterboxed.
 * Without a window (see d3d8_vk.h) it is drawn all the same.
 *
 * One thread draws: the push-buffer executor's. Nothing here is locked.
 */

#include "d3d8_xbox.h"
#include "d3d8_swizzle.h"
#include "d3d8_combiners.h"
#include "d3d8_vk.h"
#include "d3d8_present.h"  /* d3d8_canvas_width */
#include "d3d8_readback.h"
#include "d3d8_vk_cache.h"
#include "d3d8_vk_rt.h"
#include "visual_scene_wsi.h"
#include "win32_compat.h"   /* GetTickCount64 */

#include <vulkan/vulkan.h>
#include "sprite_xbr_vk.h"
#include "d3d8_vk_snapshot.h"
#include "vk_resource_versions.h"
_Static_assert(RJP_WRITES>=VKR_ENTRIES,"producer ledger must cover every live backend image");
#include <shaderc/shaderc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int scene_source_active(void);
static void scene_source_receipt(void);
static int scene_wsi_defer(void);
static int scene_wsi_wait_ok(VkResult);
static void scene_wsi_changed(void);

#ifndef D3D_OK
#define D3D_OK ((HRESULT)0)
#endif
#ifndef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086CL)
#endif

UINT d3d8_format_bpp(D3DFORMAT fmt);
/* Proposal disabled until explicit pre-device configure; executor owner only. */
static D3D8VkSnapshotOwner sn_owner;
static void *sn_owner_user;
static int sn_enabled,sn_live;
static uint64_t sn_device_generation=1;
static uint64_t sn_draw_serial;
static VfpGeneration sn_generation;
static VkResourceVersions sn_versions;
static VksContext *sn_context;
static int sn_accessible(void){return sn_enabled&&sn_owner&&sn_owner(sn_owner_user)&&!sn_versions.broken;}
static void sn_touch(VkImage image){
 if(!sn_enabled||!image)return;
 if(sn_live&&!sn_accessible()){sn_versions.broken=1;return;}
 (void)vkr_touch(&sn_versions,image);
}
static void sn_destroy_image(VkDevice device,VkImage image,const VkAllocationCallbacks*allocation){
 if(sn_enabled){
  if(sn_live&&!sn_accessible())sn_versions.broken=1;
  /* Copy the real identity before removal. Cached/inactive targets and
   * borrowed Surface aliases remain live and never reach this destructor. */
  if(rjp_enabled()){VksSource*source=vkr_image(&sn_versions,image);if(source)rjp_forget_allocation(source->storage_identity);}
  vkr_forget(&sn_versions,image);
 }
 vkDestroyImage(device,image,allocation);
}


/* Exact low-level writer coverage; RAW callbacks may only account/classify.
 * The original Vulkan operation still executes exactly once. djultra64. */
#include "visual_scene_journal_backend.h"
#define vkCmdDraw(...) (vsj_backend_raw(VSJ_RAW_DRAW,__func__,__LINE__),vkCmdDraw(__VA_ARGS__))
#define vkCmdDrawIndexed(...) (vsj_backend_raw(VSJ_RAW_INDEXED_DRAW,__func__,__LINE__),vkCmdDrawIndexed(__VA_ARGS__))
#define vkCmdClearAttachments(...) (vsj_backend_raw(VSJ_RAW_CLEAR_ATTACHMENTS,__func__,__LINE__),vkCmdClearAttachments(__VA_ARGS__))
#define vkCmdClearColorImage(...) (vsj_backend_raw(VSJ_RAW_CLEAR_IMAGE,__func__,__LINE__),vkCmdClearColorImage(__VA_ARGS__))
#define vkCmdCopyBufferToImage(...) (vsj_backend_raw(VSJ_RAW_UPLOAD,__func__,__LINE__),vkCmdCopyBufferToImage(__VA_ARGS__))
#define vkCmdBlitImage(...) (vsj_backend_raw(VSJ_RAW_BLIT,__func__,__LINE__),vkCmdBlitImage(__VA_ARGS__))

#define MAX_RS      512
#define STAGES      4
#define LOG(...)    do { fprintf(stderr, "[D3D8-VK] " __VA_ARGS__); fputc('\n', stderr); } while (0)

/* ── Host, settings ────────────────────────────────────────────────────── */

static D3D8VkHost s_host;
static int        s_have_host;

void d3d8_vk_set_host(const D3D8VkHost *host)
{
    s_have_host = host != NULL;
    if (host)
        s_host = *host;
}

static D3D8VkOverlay s_overlay;
static int            s_have_overlay;

void d3d8_vk_set_overlay(const D3D8VkOverlay *overlay)
{
    s_have_overlay = overlay != NULL && overlay->active && overlay->draw;
    if (overlay)
        s_overlay = *overlay;
}

/* The shader cache's folder (d3d8_vk.h). RECOMP_SHADER_CACHE overrides it: a
 * folder, or 0 / off for none. */
static char s_cache_dir[1024];
void d3d8_vk_set_cache_dir(const char *dir)
{
    snprintf(s_cache_dir, sizeof s_cache_dir, "%s", dir ? dir : "");
}

/* Bump when the SPIR-V for the same HLSL text would change: the shaderc
 * options in resources_make, the binding bases, the entry point. The text
 * itself (the translators' output) is in the key already. */
#define SHADER_CACHE_VERSION 1u

#define D3D8_VK_GUEST_RASTER_SCALE 1
static unsigned s_render_scale = 1;
static uint32_t s_guest_raster_num=1,s_guest_raster_den=1;
static int s_guest_raster_explicit;
static int      s_scaling, s_keep_aspect = 1, s_linear = 1, s_vsync;
static unsigned s_aspect_num = 4, s_aspect_den = 3;
/* Widescreen canvas (d3d8_present.h): the title's target widened to num:den. */
static unsigned s_canvas_num, s_canvas_den;
/* Widescreen 2D placement for the next draws (nv2a_pb_exec.c, ws_place_batch). */
static float s_place_kx = 1.0f, s_place_bx = 0.0f;
float d3d8_GetCanvasScaleX(void);
float d3d8_GetCanvasScaleY(void);

void d3d8_present_enable_scaling(int enable)       { s_scaling = enable; }
void d3d8_present_set_render_scale(unsigned scale) { s_render_scale = scale < 1 ? 1 : scale > 4 ? 4 : scale; }
void d3d8_present_set_canvas_aspect(unsigned num, unsigned den) { s_canvas_num = num; s_canvas_den = den; }
void d3d8_present_set_aspect(int keep, unsigned num, unsigned den)
{
    s_keep_aspect = keep;
    if (num && den) { s_aspect_num = num; s_aspect_den = den; }
}
void d3d8_present_set_vsync(int vsync)             { s_vsync = vsync; }
void d3d8_present_set_linear_filter(int linear)    { s_linear = linear; }
/* The title's own 60 Hz timer paces the frames here; the display never does. */
int  d3d8_present_display_paced(void)              { return 0; }
/* The title's gamma ramp, applied to what the window shows (gamma_pass below).
 * Captures read the picture before it and apply the ramp themselves. */
static float s_gamma[256][4];
static int   s_gamma_on, s_gamma_dirty;
void d3d8_gamma_set(const void *ramp)
{
    const D3DGAMMARAMP *r = ramp;
    int i, on = 0;
    if (!r)
        return;
    for (i = 0; i < 256; i++) {
        s_gamma[i][0] = (float)r->red[i] / 65535.0f;
        s_gamma[i][1] = (float)r->green[i] / 65535.0f;
        s_gamma[i][2] = (float)r->blue[i] / 65535.0f;
        s_gamma[i][3] = 1.0f;
        if (r->red[i] != i * 257 || r->green[i] != i * 257 || r->blue[i] != i * 257)
            on = 1;
    }
    s_gamma_on = on;
    s_gamma_dirty = 1;
}

static const char *g_window_title = "Xbox Game";
void xbox_D3D8SetWindowTitle(const char *title)    { if (title && *title) g_window_title = title; }
volatile int g_suppress_present = 0;

/* ── Vulkan state ──────────────────────────────────────────────────────── */

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    uint8_t       *map;
    VkDeviceSize   size, at;
    int            coherent;    /* 0: host-cached only; invalidate before the CPU reads */
} Ring;

typedef struct { VkImage img; VkDeviceMemory mem; VkImageView view; } Retired;

#define MAX_TEX_LEVELS 15

typedef struct VkTex {
    IDirect3DTexture8 iface;
    LONG       ref;
    int        cube;           /* six layers, same base interface prefix */
    BYTE      *faces[5][MAX_TEX_LEVELS]; /* face0 uses sys/lvl */
    uint32_t   face_have[6],face_locked[6];
    int        borrowed;       /* render-target cache owns image/allocation */
    UINT       width, height;
    D3DFORMAT  format;
    BYTE      *sys;             /* what LockRect hands out: the title's bytes */
    UINT       pitch, sys_bytes;
    /* Mip chain (texture packs). Level 0 is sys/pitch; the others are owned here. */
    UINT       levels;
    BYTE      *lvl[MAX_TEX_LEVELS];
    UINT       lvl_pitch[MAX_TEX_LEVELS];
    uint32_t   have;            /* levels the title wrote since the image was made */
    VkImage        img;
    VkDeviceMemory mem;
    VkImageView    view;
    VkImageView sprite_view;
    SxVkAuth sprite_auth;
    uint64_t sprite_storage,sprite_version;
    uint64_t   used_frame;      /* last frame a draw sampled img */
} VkTex;

static VkImageView sx_view(const VkTex*,unsigned);
static void sx_device_ready(void);
#include "d3d8_sprite_xbr.h"
static const IDirect3DTexture8Vtbl g_tex_vtbl;
static const IDirect3DCubeTexture8Vtbl g_cube_vtbl;

typedef struct {
    IDirect3DSurface8 iface;
    LONG  ref;
    BYTE *pixels;
} VkBackSurface;

/* Everything a pipeline is built from. Compared and hashed as bytes, so it
 * is cleared with memset before it is filled. */
typedef struct {
    uint32_t vs, ps;            /* shader ids, 1-based */
    uint32_t color_format;      /* render-pass attachment compatibility */
    uint8_t  layout;            /* 0 fixed vertex, else float4 attribute count */
    uint8_t  instanced;
    uint8_t  topology;
    uint8_t  blend, src, dst, op;
    uint8_t  cw;
    uint8_t  cull;
    uint8_t  ztest, zfunc, zwrite, has_depth;
    uint8_t  polygon_offset;
    uint8_t  stencil, sfunc, sfail, szfail, spass;
} PipeKey;

/* Stable entries: descriptor sets and commands can retain these image views.
 * No eviction until allocation lifetime/invalidation is supplied by the host. */
#define RT_COLOR_LIMIT 4096u
#define RT_DEPTH_LIMIT 1024u
#define RT_GPU_BUDGET (UINT64_C(256)*1024u*1024u)
typedef struct {
    uint32_t addr,w,h,pitch,zformat,raster_width,raster_height;
    VkImage img; VkDeviceMemory mem; VkImageView view;
    VkImageLayout layout;
    VkTex sample; /* borrowed image, depth-only view for sampler2D */
} RtDepth;
typedef struct {
    D3D8VkTarget desc;
    uint32_t raster_width,raster_height;
    int depth_only,current;
    VkFormat color_format;
    unsigned color_bytes;
    VkTex tex;
    VkTex normalized; /* stable per-view metadata; image owned by tex/cache */
    VkImageLayout layout;
    VkRenderPass pass;
    VkFramebuffer fb;
    RtDepth *depth;
} RtColor;
/* The index may grow, but objects and embedded borrowed interfaces never move.
 * 256MiB is an explicit RT-only allocation budget, not reported physical VRAM.
 * Actual VkMemoryRequirements.size includes driver alignment. Guest memory,
 * ordinary textures and the host swapchain are separate accounting domains. */
static RtDepth **rt_depths;
static RtColor **rt_colors;
static unsigned rt_color_n,rt_depth_n,rt_color_cap,rt_depth_cap;
static uint64_t rt_gpu_bytes;
static void **rt_index_prepare(void **old,unsigned cap,unsigned n,unsigned limit,unsigned *next_cap)
{
    *next_cap=cap;
    if(n>=limit)return NULL;
    if(n<cap)return old;
    unsigned wanted=cap?cap*2u:64u;if(wanted>limit)wanted=limit;
    void **next=malloc((size_t)wanted*sizeof *next);if(!next)return NULL;
    if(n)memcpy(next,old,(size_t)n*sizeof *next);
    *next_cap=wanted;return next;
}
static RtColor *rt_active;
static RtColor *rt_presented;
void d3d8_SetScissorRect(UINT x,UINT y,UINT width,UINT height);

typedef struct { PipeKey key; VkPipeline pipe; int used; } PipeEntry;
#define PIPE_SLOTS 1024

typedef struct { uint32_t hash, used; int points, failed; uint32_t shader; uint64_t last; } ProgEntry;
#define PROG_SLOTS 256
typedef struct { int used; NV2ACombinerState st; uint32_t shader; } CombEntry;
#define COMB_SLOTS 256
#define MAX_SHADERS 1024
/* Each HLSL text made into a module this run, by two hashes of it and its
 * stage: the same text gets the same module. Twice MAX_SHADERS, never full. */
typedef struct { uint64_t key, check; uint32_t id; } ShaderSeen;
#define SEEN_SLOTS (2 * MAX_SHADERS)

static struct {
    int ready;
    uint32_t max_raster_width,max_raster_height;
    VkInstance       inst;
    VkPhysicalDevice phys;
    VkDevice         dev;
    uint32_t         qfam;
    VkQueue          queue;
    VkPhysicalDeviceMemoryProperties memprops;
    VkDeviceSize     ubo_align;

    VkSurfaceKHR     surface;
    VkSwapchainKHR   swap;
    VkImage          swap_img[8];
    uint32_t         swap_n;
    VkExtent2D       swap_ext;
    VkFormat         swap_fmt;
    VkImageUsageFlags swap_usage; /* successful swapchain image contract */
    /* The overlay's pass and, per swap-chain image, what it draws into. */
    VkRenderPass     ov_pass;
    VkFormat         ov_fmt;
    uint32_t         ov_gen;
    VkImageView      ov_view[8];
    VkFramebuffer    ov_fb[8];

    uint32_t lw, lh, scale, tw, th;         /* title size, scale, target size */
    VkFormat       depth_fmt;
    VkImage        color, depth;
    VkDeviceMemory color_mem, depth_mem;
    VkImageView    color_view, depth_view;
    VkRenderPass   pass;
    VkFramebuffer  fb;

    /* The picture after the gamma ramp, which is what the window is given. */
    VkImage        final;
    VkDeviceMemory final_mem;
    VkImageView    final_view;
    VkRenderPass   gamma_pass;
    VkFramebuffer  gamma_fb;
    VkDescriptorSetLayout gamma_dsl;
    VkPipelineLayout      gamma_pl;
    VkDescriptorPool      gamma_pool;
    VkDescriptorSet       gamma_set;
    VkPipeline            gamma_pipe;
    Ring                  gamma_ubo;

    VkCommandPool   pool;
    VkCommandBuffer cmd, cmd_up;
    VkFence         fence;
    VkSemaphore     acquired;       /* signalled when a swap-chain image is ours */
    int      frame_open, in_pass;
    uint64_t frame;

    Ring vtx, idx, ubo, stage, readback;

    VkDescriptorSetLayout dsl;
    VkPipelineLayout      pl;
    VkDescriptorPool      dpool;
    VkSampler             samplers[4][4][2][2][3][2];
    float                 max_anisotropy; /* enabled device limit;1 disables */
    VkImage        dummy;
    VkDeviceMemory dummy_mem;
    VkImageView    dummy_view;
    VkImage dummy_cube;VkDeviceMemory dummy_cube_mem;VkImageView dummy_cube_view;

    shaderc_compiler_t        sc;
    shaderc_compile_options_t sc_opts;
    VkPipelineCache pcache;
    VkcDevice       pcache_dev;
    uint32_t        pcache_new;     /* pipelines made since the cache was last written */
    uint64_t        pcache_hash;    /* of the data last read or written: unchanged is not written */
    ULONGLONG       pcache_last_new;
    /* What shaders and pipelines cost: counts and microseconds. */
    uint32_t spv_hits, spv_misses, spv_reused, pipes_made;
    uint64_t spv_load_us, spv_compile_us, pipes_us;
    uint32_t perf_printed;
    VkShaderModule shaders[MAX_SHADERS];
    uint32_t       shader_n;
    ShaderSeen     seen[SEEN_SLOTS];
    uint32_t       vs_fixed, ps_tex, ps_notex;

    PipeEntry pipes[PIPE_SLOTS];
    ProgEntry progs[PROG_SLOTS];
    int       prog_n;
    uint64_t  prog_tick;
    CombEntry combs[COMB_SLOTS];

    Retired  retired[1024];
    int      retired_n;

    /* What the command buffer currently has bound. */
    VkPipeline      bound_pipe;
    VkDescriptorSet bound_set;
    VkImageView     set_views[STAGES];
    VkSampler       set_samplers[STAGES];
    uint32_t        dyn_off[3];
    int             scissor_dirty;

    /* Device state as the translator set it. */
    DWORD  rs[MAX_RS];
    DWORD  tss[STAGES][32];
    VkTex *tex[STAGES];
    uint32_t scissor[4];            /* x, y, w, h in title pixels; w 0 = none */
    float  blend_color[4];
    int    polygon_offset;
    float  polygon_slope, polygon_units;
    int    points;
    float  point_size;
    uint32_t consts_version;
    uint32_t consts_off;
    int      consts_frame_valid;
} vk;

static IDirect3DDevice8 g_device;
static int g_device_made;
/* Immutable after device creation: cached borrowed views remain valid until
 * exit. Added density applies to RGBA and matching depth; procedural RGB565
 * and independent depth-only shadow products keep their original resolution.
 * Own integration: djultra64. */
int d3d8_vk_set_guest_raster_scale(uint32_t n,uint32_t d)
{
    if(vk.ready || vk.dev || g_device_made || !n || !d || n>16384u || d>16384u
       || n<d || (uint64_t)n>3u*(uint64_t)d) return 0;
    uint32_t a=n,b=d;while(b){uint32_t r=a%b;a=b;b=r;}
    s_guest_raster_num=n/a;s_guest_raster_den=d/a;s_guest_raster_explicit=1;
    return 1;
}
/* pass_begin uses a full-attachment viewport with y=height,height=-height.
 * Thus both endpoints [0,extent] must fit the chosen device's viewport range,
 * independently of image/framebuffer limits. Never round a float bound up. */
static int raster_limits_from_properties(const VkPhysicalDeviceLimits *p,uint32_t *w,uint32_t *h)
{
    if(!p || !w || !h || !isfinite(p->viewportBoundsRange[0])
       || !isfinite(p->viewportBoundsRange[1]) || p->viewportBoundsRange[0]>0
       || p->viewportBoundsRange[1]<1) return 0;
    uint32_t x=p->maxImageDimension2D,y=x;
    if(x>p->maxFramebufferWidth)x=p->maxFramebufferWidth;
    if(y>p->maxFramebufferHeight)y=p->maxFramebufferHeight;
    if(x>p->maxViewportDimensions[0])x=p->maxViewportDimensions[0];
    if(y>p->maxViewportDimensions[1])y=p->maxViewportDimensions[1];
    double bound=floor((double)p->viewportBoundsRange[1]);
    if((double)x>bound)x=(uint32_t)bound;
    if((double)y>bound)y=(uint32_t)bound;
    if(!x||!y)return 0;
    *w=x;*h=y;return 1;
}
static int raster_size(uint32_t w,uint32_t h,int denser,uint32_t *rw,uint32_t *rh)
{
    uint32_t n=denser?s_guest_raster_num:1,d=denser?s_guest_raster_den:1;
    uint64_t x=((uint64_t)w*n+d/2u)/d,y=((uint64_t)h*n+d/2u)/d;
    uint32_t mw=vk.max_raster_width?vk.max_raster_width:4096u;
    uint32_t mh=vk.max_raster_height?vk.max_raster_height:4096u;
    if(!x||!y||x>mw||y>mh)return 0;
    *rw=(uint32_t)x;*rh=(uint32_t)y;return 1;
}
int d3d8_vk_raster_limits(uint32_t *w,uint32_t *h)
{
    if(!vk.ready||!w||!h||!vk.max_raster_width||!vk.max_raster_height)return 0;
    *w=vk.max_raster_width;*h=vk.max_raster_height;return 1;
}


/* ── Small helpers ─────────────────────────────────────────────────────── */

#define VKC(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    LOG("%s failed (%d) at line %d", #call, (int)r_, __LINE__); } } while (0)

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < vk.memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (vk.memprops.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

static uint32_t mem_type_any(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid)
{
    uint32_t i;
    for (i = 0; i < vk.memprops.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = vk.memprops.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (f & want) == want && !(f & avoid))
            return i;
    }
    return UINT32_MAX;
}

/* `readback`: the CPU reads this buffer, so prefer host-cached memory. Plain
 * host-visible memory can be uncached (write-combined) -- the Steam Deck's APU
 * is one -- where reading a picture back costs hundreds of milliseconds.
 * Without a cached type the buffer is the plain one it always was. */
static int ring_make_ex(Ring *r, VkDeviceSize size, VkBufferUsageFlags usage, int readback)
{
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements mr;
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    void *p = NULL;
    uint32_t type = UINT32_MAX;
    int coherent = 1;

    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(vk.dev, &bi, NULL, &r->buf) != VK_SUCCESS)
        return 0;
    vkGetBufferMemoryRequirements(vk.dev, r->buf, &mr);
    ai.allocationSize = mr.size;
    if (readback) {
        type = mem_type_any(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
        if (type == UINT32_MAX) {
            type = mem_type_any(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
            coherent = 0;
        }
    }
    if (type == UINT32_MAX) {
        type = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                           | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        coherent = 1;
    }
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(vk.dev, &ai, NULL, &r->mem) != VK_SUCCESS
            || vkBindBufferMemory(vk.dev, r->buf, r->mem, 0) != VK_SUCCESS
            || vkMapMemory(vk.dev, r->mem, 0, size, 0, &p) != VK_SUCCESS)
        return 0;
    r->map = p;
    r->size = size;
    r->at = 0;
    r->coherent = coherent;
    return 1;
}

static int ring_make(Ring *r, VkDeviceSize size, VkBufferUsageFlags usage)
{
    return ring_make_ex(r, size, usage, 0);
}

/* Make a finished GPU write visible to the CPU (a no-op on coherent memory). */
static void ring_invalidate(const Ring *r)
{
    VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
    if (r->coherent)
        return;
    range.memory = r->mem;
    range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(vk.dev, 1, &range);
}

static int image_make_layers_budget(uint32_t w, uint32_t h, uint32_t levels, uint32_t layers, int cube, VkFormat fmt, VkImageUsageFlags usage,
                             VkImageAspectFlags aspect, VkImage *img, VkDeviceMemory *mem,
                             VkImageView *view, VkDeviceSize available, VkDeviceSize *charge)
{
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkMemoryRequirements mr;
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageView image_view=VK_NULL_HANDLE;

    ii.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent.width = w; ii.extent.height = h; ii.extent.depth = 1;
    ii.mipLevels = levels; ii.arrayLayers = layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage | (sn_enabled?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    if (vkCreateImage(vk.dev, &ii, NULL, &image) != VK_SUCCESS)
        return 0;
    vkGetImageMemoryRequirements(vk.dev, image, &mr);
    if(mr.size>available)goto fail;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk.dev, &ai, NULL, &memory) != VK_SUCCESS
            || vkBindImageMemory(vk.dev, image, memory, 0) != VK_SUCCESS)
        goto fail;
    vi.image = image;
    vi.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = levels;
    vi.subresourceRange.layerCount = layers;
    if(vkCreateImageView(vk.dev,&vi,NULL,&image_view)!=VK_SUCCESS)goto fail;
    if(sn_enabled){
        VksSource source={.image=image,.format=fmt,.aspects=aspect,.usage=ii.usage,
          .flags=ii.flags,.samples=ii.samples,.width=w,.height=h,.mips=levels,
          .layers=layers,.device_generation=sn_device_generation};
        if(sn_live&&!sn_accessible())sn_versions.broken=1;
        else (void)vkr_add(&sn_versions,&source);
    }
    *img=image;*mem=memory;*view=image_view;if(charge)*charge=mr.size;return 1;
fail:
    if(image_view)vkDestroyImageView(vk.dev,image_view,NULL);
    if(image)sn_destroy_image(vk.dev,image,NULL);
    if(memory)vkFreeMemory(vk.dev,memory,NULL);
    return 0;
}

static int image_make_layers(uint32_t w,uint32_t h,uint32_t levels,uint32_t layers,int cube,VkFormat fmt,VkImageUsageFlags usage,
                             VkImageAspectFlags aspect,VkImage *img,VkDeviceMemory *mem,VkImageView *view)
{return image_make_layers_budget(w,h,levels,layers,cube,fmt,usage,aspect,img,mem,view,UINT64_MAX,NULL);}

static int image_make_levels(uint32_t w,uint32_t h,uint32_t levels,VkFormat fmt,VkImageUsageFlags usage,
                             VkImageAspectFlags aspect,VkImage *img,VkDeviceMemory *mem,VkImageView *view)
{return image_make_layers(w,h,levels,1,0,fmt,usage,aspect,img,mem,view);}

static int image_make(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, VkImage *img, VkDeviceMemory *mem,
                      VkImageView *view)
{
    return image_make_levels(w, h, 1, fmt, usage, aspect, img, mem, view);
}

static void image_layout_layers(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect,
                                VkImageLayout from, VkImageLayout to, uint32_t base, uint32_t count,
                                uint32_t layer,uint32_t layers)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseMipLevel = base;
    b.subresourceRange.levelCount = count;
    b.subresourceRange.baseArrayLayer = layer;
    b.subresourceRange.layerCount = layers;
    b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

static void image_layout_levels(VkCommandBuffer cb,VkImage img,VkImageAspectFlags aspect,
                                VkImageLayout from,VkImageLayout to,uint32_t base,uint32_t count)
{image_layout_layers(cb,img,aspect,from,to,base,count,0,1);}

static void image_layout(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect,
                         VkImageLayout from, VkImageLayout to)
{
    image_layout_levels(cb, img, aspect, from, to, 0, 1);
}

static void retire(VkImage img, VkDeviceMemory mem, VkImageView view)
{
    if (!img)
        return;
    if (vk.retired_n < (int)(sizeof vk.retired / sizeof vk.retired[0])) {
        Retired *r = &vk.retired[vk.retired_n++];
        r->img = img; r->mem = mem; r->view = view;
    }
    /* Past that the image leaks rather than being destroyed while a command
     * buffer may still name it; a frame does not replace a thousand textures. */
}

/* ── Frames ────────────────────────────────────────────────────────────── */

static void frame_begin(void)
{
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    if (vk.frame_open)
        return;
    VKC(vkResetCommandPool(vk.dev, vk.pool, 0));
    VKC(vkResetDescriptorPool(vk.dev, vk.dpool, 0));
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKC(vkBeginCommandBuffer(vk.cmd, &bi));
    VKC(vkBeginCommandBuffer(vk.cmd_up, &bi));
    vk.vtx.at = vk.idx.at = vk.ubo.at = vk.stage.at = 0;
    vk.frame_open = 1;
    vk.in_pass = 0;
    vk.bound_pipe = VK_NULL_HANDLE;
    vk.bound_set = VK_NULL_HANDLE;
    vk.consts_frame_valid = 0;
    vk.frame++;
}

static void pass_begin(void)
{
    VkRenderPassBeginInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    VkViewport vp;

    frame_begin();
    if (vk.in_pass)
        return;
    rp.renderPass = rt_active ? rt_active->pass : vk.pass;
    rp.framebuffer = rt_active ? rt_active->fb : vk.fb;
    rp.renderArea.extent.width = rt_active ? rt_active->raster_width : vk.tw;
    rp.renderArea.extent.height = rt_active ? rt_active->raster_height : vk.th;
    vkCmdBeginRenderPass(vk.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    /* The shaders produce Direct3D clip space, y up. A viewport with a
     * negative height turns that the right way for Vulkan. */
    vp.x = 0.0f; vp.y = (float)rp.renderArea.extent.height;
    vp.width = (float)rp.renderArea.extent.width; vp.height = -(float)rp.renderArea.extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(vk.cmd, 0, 1, &vp);
    vk.in_pass = 1;
    vk.bound_pipe = VK_NULL_HANDLE;
    vk.bound_set = VK_NULL_HANDLE;
    vk.scissor_dirty = 1;
}

static void sn_targets(unsigned flags){
 if(!sn_enabled)return;
 if(flags&1u)sn_touch(rt_active?(rt_active->depth_only?VK_NULL_HANDLE:rt_active->tex.img):vk.color);
 if(flags&2u)sn_touch(rt_active?(rt_active->depth?rt_active->depth->img:VK_NULL_HANDLE):vk.depth);
 if(rjp_enabled()){
  if(!rt_active)rjp_coverage_unknown();
  else for(unsigned role=0;role<2;role++)if(flags&(1u<<role)){
   RtColor*c=rt_active;uint32_t address=role?c->desc.zeta:(c->depth_only?0:c->desc.color);
   uint32_t pitch=role?c->desc.zeta_pitch:c->desc.color_pitch;
   uint64_t bytes=(uint64_t)pitch*c->desc.height;
   if(address){uint32_t base,size;uint64_t epoch=0;
    if(!bytes||bytes>UINT32_MAX)rjp_coverage_unknown();
    else {(void)xbox_ContiguousEpoch(address,(uint32_t)bytes,&base,&size,&epoch);rjp_gpu_written(address,(uint32_t)bytes,epoch);}
   }
  }
 }
 if((flags&1u)&&rjp_enabled()&&sn_accessible()&&rt_active&&!rt_active->depth_only&&
    (rt_active->desc.format&255u)==0x28u&&!(rt_active->desc.format&0xf000u)){
  RtColor*c=rt_active;uint32_t base,size;uint64_t epoch;
  VksSource*source=vkr_image(&sn_versions,c->tex.img);
  uint64_t span=(uint64_t)c->desc.color_pitch*c->desc.height;
  if(source&&span<=UINT32_MAX&&xbox_ContiguousEpoch(c->desc.color,(uint32_t)span,&base,&size,&epoch))
   rjp_write_native_rgba(c->desc.color,epoch,source->storage_identity,source->content_version,c->desc.width,c->desc.height,c->desc.color_pitch);
 }

}

static void pass_end(void)
{
    if (vk.in_pass) {
        vkCmdEndRenderPass(vk.cmd);
        vk.in_pass = 0;
    }
}

/* Hand the frame's commands to the GPU and wait for them. Uploads first:
 * they were recorded apart so they need not interrupt the render pass. */
#include "driver_present_timing.inc"
static uint64_t g_fence_wait_ticks, g_acquire_ticks, g_present_ticks;   /* time spent waiting for the GPU, for the 2 s line */
/* Submits that have completed. Every submit waits on its fence, so a command
 * recorded before submit n has finished once this is n or more. */
static uint64_t g_submits_done;
static void frame_submit_wait(VkSemaphore wait)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandBuffer cbs[2];
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    int i;

    if (!vk.frame_open)
        return;
    pass_end();
    DPT_VKC(DPT_COMMAND, vkEndCommandBuffer(vk.cmd_up));
    DPT_VKC(DPT_COMMAND, vkEndCommandBuffer(vk.cmd));
    cbs[0] = vk.cmd_up;
    cbs[1] = vk.cmd;
    si.commandBufferCount = 2;
    si.pCommandBuffers = cbs;
    if (wait) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &wait;
        si.pWaitDstStageMask = &stage;
    }
    DPT_VKC(DPT_COMMAND, vkResetFences(vk.dev, 1, &vk.fence));
    DPT_VKC(DPT_SUBMIT, vkQueueSubmit(vk.queue, 1, &si, vk.fence));
    {
        LARGE_INTEGER a, b;
        int dpt_a_ok=QueryPerformanceCounter(&a);
        DPT_VKC(DPT_FENCE, vkWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX));
        int dpt_b_ok=QueryPerformanceCounter(&b);
        dpt_ticks(DPT_FENCE,dpt_a_ok&&dpt_b_ok,dpt_a_ok?a.QuadPart:0,dpt_b_ok?b.QuadPart:0);
        g_fence_wait_ticks += (uint64_t)(b.QuadPart - a.QuadPart);
    }
    g_submits_done++;
    vk.frame_open = 0;
    for (i = 0; i < vk.retired_n; i++) {
        vkDestroyImageView(vk.dev, vk.retired[i].view, NULL);
        sn_destroy_image(vk.dev, vk.retired[i].img, NULL);
        vkFreeMemory(vk.dev, vk.retired[i].mem, NULL);
    }
    vk.retired_n = 0;
}

static void frame_submit(void) { frame_submit_wait(VK_NULL_HANDLE); }

static RtColor *rt_find(uint32_t addr)
{
    unsigned i;
    for(i=0;i<rt_color_n;i++)
        if(rt_colors[i]->current && rt_colors[i]->desc.color==addr) return rt_colors[i];
    return NULL;
}

/* End writes before sampling, and end reads before attachment writes. All
 * targets use the same graphics queue and command buffer as ordinary draws. */
static void rt_layout(RtColor *c,VkImageLayout to)
{
    if(c->layout==to) return;
    frame_begin(); pass_end();
    image_layout(vk.cmd,c->tex.img,VK_IMAGE_ASPECT_COLOR_BIT,c->layout,to);
    c->layout=to;
    vk.bound_set=VK_NULL_HANDLE;
}

static void rt_depth_layout(RtDepth *z, VkImageLayout to)
{
    if (z->layout == to) return;
    frame_begin(); pass_end();
    image_layout(vk.cmd,z->img,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,z->layout,to);
    z->layout=to;
    vk.bound_set=VK_NULL_HANDLE;
}

/* A shared zeta image can stay in attachment layout across different render
 * passes. That still needs an execution AND memory dependency: a layout
 * transition on the unrelated color image does not synchronize its writes. */
static void rt_attachment_barrier(void)
{
    VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    frame_begin();pass_end();
    b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                  |VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(vk.cmd,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                         0,1,&b,0,NULL,0,NULL);
}

/* CPU-only bounded failure evidence; own work: djultra64. No readback,
 * allocation, eviction or extra Vulkan query. Manual runs need no new flag. */
static int rt_select_failure(const D3D8VkTarget *d,const char *reason,int result)
{
    static int enabled=-1;
    static unsigned reported;
    if(enabled<0) { const char *e=getenv("RECOMP_RT_FAILURE_TRACE"); enabled=!e || strcmp(e,"0"); }
    if(enabled && reported<8) {
        const D3D8VkTarget zero={0};
        if(!d)d=&zero;
        reported++;
        LOG("[RT-FAIL] reason=%s result=%d colors=%u/%u depths=%u/%u color=%08X zeta=%08X format=%08X extent=%ux%u pitch=%u/%u clip=%ux%u+%u+%u minimum=%u gpu_bytes=%llu gpu_budget=%llu",
            reason,result,rt_color_n,RT_COLOR_LIMIT,rt_depth_n,RT_DEPTH_LIMIT,
            d->color,d->zeta,d->format,d->width,d->height,d->color_pitch,d->zeta_pitch,
            d->clip_width,d->clip_height,d->clip_x,d->clip_y,d->extent_is_minimum,
            (unsigned long long)rt_gpu_bytes,(unsigned long long)RT_GPU_BUDGET);
    }
    return result;
}

int d3d8_vk_rt_select_vsj_body(const D3D8VkTarget *d)
{
    RtColor *c; RtDepth *z=NULL;
    unsigned i;
    const D3D8VkTarget *requested=d;
    const VkImageAspectFlags za=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!vk.ready) return rt_select_failure(requested,"not_ready",-1);
    if(!d) { if(!rt_active)return 1; rt_attachment_barrier(); rt_active=NULL; return 1; }
    /* Top Spin clears a nonzero RGB565 allocation in pitch mode (0113),
     * then draws into that same allocation in swizzled mode (0213). Both
     * address the same host attachment; guest memory layout is not a second
     * GPU allocation. Keep color-zero 0113 as the independent depth target. */
    D3D8VkTarget canonical=*d;
    if(d->color && (d->format&0xFFFFu)==0x0113u) {
        canonical.format=(d->format&~0xF00u)|0x200u;
        d=&canonical;
    }
    /* The same RGBA allocation is cleared pitched, then drawn swizzled.
     * Texture views retain their own coordinate convention independently. */
    if(d->color && (d->format&0xffffu)==0x0228u) {
        canonical.format=(d->format&~0xf00u)|0x100u;
        d=&canonical;
    }
    uint32_t cx=d->clip_x,cy=d->clip_y,cw=d->clip_width,ch=d->clip_height;
    int clipped=cw && ch;
    int minimum=d->extent_is_minimum!=0;
    canonical=*d;
    canonical.clip_x=canonical.clip_y=canonical.clip_width=canonical.clip_height=0;
    canonical.extent_is_minimum=0;
    d=&canonical;
    /* Surface clip is a minimum bound, not a new allocation declaration.
     * Preserve a compatible known allocation and its screen-space mapping.
     * Explicit dimensions (legacy callers/real redefinitions) stay exact. */
    RtColor *previous=rt_find(d->color);
    if(minimum && previous && previous->desc.format==d->format
       && previous->desc.zeta==d->zeta && previous->desc.color_pitch==d->color_pitch
       && previous->desc.zeta_pitch==d->zeta_pitch) {
        if(canonical.width<previous->desc.width) canonical.width=previous->desc.width;
        if(canonical.height<previous->desc.height) canonical.height=previous->desc.height;
    }
    if(clipped && ((uint64_t)cx+cw>d->width || (uint64_t)cy+ch>d->height)) return rt_select_failure(requested,"clip_bounds",-1);
    /* AA0 RGBA/RGB565 and Z16, plus opt-in AA1 regular 2x-horizontal
     * backing. This preserves sample-slot storage/aliasing, not verified
     * NV2A CENTER_CORNER_2 raster sample locations. */
    int aa1=(d->format&0xffffu)==0x1128u;
    if(aa1) {
        static int enabled=-1;
        if(enabled<0) { const char *e=getenv("RECOMP_GUEST_AA1");enabled=e && !strcmp(e,"1"); }
        if(!enabled) return rt_select_failure(requested,"aa1_disabled",0);
    }
    int rgba=(d->format&0xffffu)==0x0128u || aa1;
    unsigned color_bytes=rgba?4:2;
    VkFormat color_format=rgba?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R5G6B5_UNORM_PACK16;
    int depth_only=(d->format&0xffffu)==0x0113u && d->color==0;
    if((d->format&0xffffu)!=0x0213u && !depth_only && !rgba) return rt_select_failure(requested,"unsupported_format",0);
    uint64_t backing_width=(uint64_t)d->width*(aa1?2u:1u);
    if((!d->color && !depth_only) || (!d->zeta && !rgba) || !d->width || !d->height || backing_width>4096 || d->height>4096
       || d->color_pitch<backing_width*color_bytes
       || (d->zeta && d->zeta_pitch<backing_width*(rgba?4u:2u))) return rt_select_failure(requested,"invalid_descriptor",-1);
    uint32_t gw=(uint32_t)backing_width,gh=d->height,pw,ph;
    if(!raster_size(gw,gh,rgba,&pw,&ph))return rt_select_failure(requested,"raster_device_limits",-1);
    c=rt_find(d->color);
    if(c && memcmp(&c->desc,d,sizeof *d)) {
        /* Guest allocation addresses are reused for different image extents.
         * Keep prior generations stable for recorded GPU commands and held
         * texture interfaces; reuse an existing compatible generation when
         * the title alternates known descriptors. Never retag a live image. */
        c=NULL;
        for(i=0;i<rt_color_n;i++)
            if(rt_colors[i]->desc.color==d->color && !memcmp(&rt_colors[i]->desc,d,sizeof *d)) {
                c=rt_colors[i];break;
            }
    }
    if(c && rt_active==c) {
        if(clipped) d3d8_SetScissorRect(cx,cy,cw,ch);
        return 1;
    }
    if(!c) {
        VkFormatProperties props;
        VkAttachmentDescription ad[2];
        VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference zr={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub={0};
        VkRenderPassCreateInfo pi={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        VkImageView views[2];
        if(rt_color_n==RT_COLOR_LIMIT) return rt_select_failure(requested,"color_pool_full",-1);
        if(!depth_only) {
            vkGetPhysicalDeviceFormatProperties(vk.phys,color_format,&props);
            if((props.optimalTilingFeatures&(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
               !=(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return rt_select_failure(requested,"unsupported_color_features",0);
        }
        for(i=0;d->zeta && i<rt_depth_n;i++) if(rt_depths[i]->addr==d->zeta) {
            z=rt_depths[i];
            if(z->w!=gw || z->h!=gh || z->raster_width!=pw || z->raster_height!=ph || z->pitch!=d->zeta_pitch
               || z->zformat!=((d->format>>4)&15u)) { z=NULL;continue; }
            break;
        }
        /* Prepare metadata transactionally. Neither index nor counts become
         * visible until every Vulkan resource succeeds. No GPU command can
         * reference these new images before commit. */
        RtDepth *fresh_z=NULL;
        RtColor **next_colors=NULL;
        RtDepth **next_depths=rt_depths;
        unsigned color_cap=rt_color_cap,depth_cap=rt_depth_cap;
        VkDeviceSize color_charge=0,depth_charge=0;
        const char *failure="metadata_allocation";
        if(d->zeta && !z && rt_depth_n==RT_DEPTH_LIMIT)return rt_select_failure(requested,"depth_pool_full",-1);
        next_colors=(RtColor**)rt_index_prepare((void**)rt_colors,rt_color_cap,rt_color_n,RT_COLOR_LIMIT,&color_cap);
        if(!next_colors)goto rt_new_fail;
        c=calloc(1,sizeof *c);if(!c)goto rt_new_fail;
        if(d->zeta && !z) {
            next_depths=(RtDepth**)rt_index_prepare((void**)rt_depths,rt_depth_cap,rt_depth_n,RT_DEPTH_LIMIT,&depth_cap);
            if(!next_depths)goto rt_new_fail;
            fresh_z=calloc(1,sizeof *fresh_z);if(!fresh_z)goto rt_new_fail;
            z=fresh_z;failure="depth_image_or_budget";
            if(!image_make_layers_budget(pw,ph,1,1,0,vk.depth_fmt,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                           za,&z->img,&z->mem,&z->view,RT_GPU_BUDGET-rt_gpu_bytes,&depth_charge))goto rt_new_fail;
            VkImageViewCreateInfo sv={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            sv.image=z->img;sv.viewType=VK_IMAGE_VIEW_TYPE_2D;sv.format=vk.depth_fmt;
            sv.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
            sv.subresourceRange.levelCount=sv.subresourceRange.layerCount=1;
            failure="depth_sample_view";
            if(vkCreateImageView(vk.dev,&sv,NULL,&z->sample.view)!=VK_SUCCESS)goto rt_new_fail;
            z->sample.iface.lpVtbl=&g_tex_vtbl;z->sample.ref=1;z->sample.borrowed=1;
            z->sample.img=z->img;z->sample.width=gw;z->sample.height=gh;
            z->sample.format=D3DFMT_LIN_D16;
            z->addr=d->zeta;z->w=gw;z->h=gh;z->raster_width=pw;z->raster_height=ph;z->pitch=d->zeta_pitch;
            z->zformat=(d->format>>4)&15u;
            z->layout=VK_IMAGE_LAYOUT_UNDEFINED;
        }
        c->desc=*d;c->depth=z;c->depth_only=depth_only;c->raster_width=pw;c->raster_height=ph;
        c->color_format=color_format;c->color_bytes=color_bytes;
        if(aa1) {
            static unsigned reported;
            if(reported++<4) LOG("experimental AA1 horizontal backing logical %ux%u, physical %ux%u; NV2A sample locations unverified",d->width,d->height,pw,ph);
        }
        failure="color_image_or_budget";
        if(!depth_only && !image_make_layers_budget(pw,ph,1,1,0,color_format,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT,&c->tex.img,&c->tex.mem,&c->tex.view,
                      RT_GPU_BUDGET-rt_gpu_bytes-depth_charge,&color_charge))goto rt_new_fail;
        c->tex.iface.lpVtbl=&g_tex_vtbl;c->tex.ref=1;c->tex.borrowed=1;
        c->tex.width=gw;c->tex.height=gh;c->tex.format=rgba?D3DFMT_LIN_A8R8G8B8:D3DFMT_R5G6B5;
        if(rgba) { c->normalized=c->tex;c->normalized.format=D3DFMT_A8R8G8B8; }
        memset(ad,0,sizeof ad);
        ad[0].format=color_format;ad[1].format=vk.depth_fmt;
        for(i=0;i<2;i++) {
            ad[i].samples=VK_SAMPLE_COUNT_1_BIT;
            ad[i].loadOp=ad[i].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
            ad[i].storeOp=ad[i].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        }
        ad[0].initialLayout=ad[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ad[1].initialLayout=ad[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        if(depth_only) zr.attachment=0;
        sub.colorAttachmentCount=depth_only?0:1;sub.pColorAttachments=depth_only?NULL:&cr;sub.pDepthStencilAttachment=z?&zr:NULL;
        pi.attachmentCount=depth_only?1:(z?2:1);pi.pAttachments=depth_only?&ad[1]:ad;pi.subpassCount=1;pi.pSubpasses=&sub;
        failure="render_pass";
        if(vkCreateRenderPass(vk.dev,&pi,NULL,&c->pass)!=VK_SUCCESS)goto rt_new_fail;
        views[0]=c->tex.view;views[1]=z?z->view:VK_NULL_HANDLE;
        fi.renderPass=c->pass;fi.attachmentCount=depth_only?1:(z?2:1);fi.pAttachments=depth_only?&views[1]:views;
        fi.width=pw;fi.height=ph;fi.layers=1;
        failure="framebuffer";
        if(vkCreateFramebuffer(vk.dev,&fi,NULL,&c->fb)!=VK_SUCCESS)goto rt_new_fail;
        c->layout=VK_IMAGE_LAYOUT_UNDEFINED;
        if(next_colors!=rt_colors){free(rt_colors);rt_colors=next_colors;rt_color_cap=color_cap;}
        if(fresh_z){
            if(next_depths!=rt_depths){free(rt_depths);rt_depths=next_depths;rt_depth_cap=depth_cap;}
            rt_depths[rt_depth_n++]=fresh_z;
        }
        rt_colors[rt_color_n++]=c;rt_gpu_bytes+=color_charge+depth_charge;
        goto rt_new_done;
rt_new_fail:
        /* Unpublished images have no recorded command references. Existing
         * targets, bindings, layout state and accounting remain unchanged. */
        if(c){
            if(c->fb)vkDestroyFramebuffer(vk.dev,c->fb,NULL);
            if(c->pass)vkDestroyRenderPass(vk.dev,c->pass,NULL);
            if(c->tex.view)vkDestroyImageView(vk.dev,c->tex.view,NULL);
            if(c->tex.img)sn_destroy_image(vk.dev,c->tex.img,NULL);
            if(c->tex.mem)vkFreeMemory(vk.dev,c->tex.mem,NULL);
            free(c);
        }
        if(fresh_z){
            if(fresh_z->sample.view)vkDestroyImageView(vk.dev,fresh_z->sample.view,NULL);
            if(fresh_z->view)vkDestroyImageView(vk.dev,fresh_z->view,NULL);
            if(fresh_z->img)sn_destroy_image(vk.dev,fresh_z->img,NULL);
            if(fresh_z->mem)vkFreeMemory(vk.dev,fresh_z->mem,NULL);
            free(fresh_z);
        }
        if(next_colors!=rt_colors)free(next_colors);
        if(next_depths!=rt_depths)free(next_depths);
        return rt_select_failure(requested,failure,-1);
rt_new_done: ;
    }
    rt_attachment_barrier();
    for(i=0;i<rt_color_n;i++)
        if(rt_colors[i]->desc.color==d->color) rt_colors[i]->current=0;
    c->current=1;
    /* Prevent accidental texture feedback after selecting a sampled target.
     * Bind attempts while writing it are rejected separately below. */
    for(i=0;i<STAGES;i++) if(vk.tex[i]==&c->tex || vk.tex[i]==&c->normalized
                             || (c->depth && vk.tex[i]==&c->depth->sample)) vk.tex[i]=NULL;
    if(c->depth) rt_depth_layout(c->depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if(!depth_only) rt_layout(c,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    rt_active=c;
    if(clipped) d3d8_SetScissorRect(cx,cy,cw,ch);
    return 1;
}
int d3d8_vk_rt_select(const D3D8VkTarget*d){
 int result=d3d8_vk_rt_select_vsj_body(d);
 if(vsj_backend_enabled())vsj_backend_event(VSJ_TARGET_SELECT,VSJ_POST,d,d?sizeof*d:0,result,__func__);
 return result;
}


int d3d8_vk_rt_present_vsj_body(uint32_t addr)
{
    if(!vk.ready) return -1;
    if(!addr) { rt_presented=NULL;return 1; }
    RtColor *c=rt_find(addr);
    if(!c || c->depth_only || c->color_bytes!=4) return 0;
    rt_presented=c;
    return 1;
}
int d3d8_vk_rt_present(uint32_t address){
 int result=d3d8_vk_rt_present_vsj_body(address);
 if(vsj_backend_enabled())vsj_backend_event(VSJ_FINAL_TARGET,VSJ_POST,&address,sizeof address,result,__func__);
 return result;
}


int d3d8_vk_rt_bind(unsigned stage,uint32_t addr,uint32_t w,uint32_t h,uint32_t fmt)
{
    RtColor *c;
    if(stage>=STAGES) return -1;
    if(fmt==D3DFMT_LIN_D16 || fmt==D3DFMT_D16) {
        int known=0;
        for(unsigned i=0;i<rt_depth_n;i++) {
            RtDepth *z=rt_depths[i];
            if(z->addr!=addr)continue;
            known=1;
            if(rt_active && rt_active->depth && rt_active->depth->addr==addr)return -1;
            if(z->zformat!=1 || z->w!=w || z->h!=h)continue;
            rt_depth_layout(z,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            vk.tex[stage]=&z->sample;vk.bound_set=VK_NULL_HANDLE;
            return 1;
        }
        return known?-1:0;
    }
    c=rt_find(addr);
    if(!c) return 0;
    if(c->depth_only) return -1; /* sampled depth is a separate future path */
    if(c==rt_active || c->tex.width!=w || c->tex.height!=h) return -1;
    if(c->color_bytes==2 ? fmt!=D3DFMT_R5G6B5
       : (fmt!=D3DFMT_LIN_A8R8G8B8 && fmt!=D3DFMT_A8R8G8B8)) return -1;
    rt_layout(c,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk.tex[stage]=fmt==D3DFMT_A8R8G8B8?&c->normalized:&c->tex;
    vk.bound_set=VK_NULL_HANDLE;
    return 1;
}

int d3d8_vk_rt_diagnostic_rgb565(uint32_t *addresses,uint32_t capacity,uint32_t *total)
{
    if(!vk.ready || !total || (capacity && !addresses))return 0;
    uint32_t count=0;
    for(unsigned i=0;i<rt_color_n;i++) {
        const RtColor *c=rt_colors[i];
        if(!c->current || c->depth_only || c->color_bytes!=2 ||
           c->tex.width!=64 || c->tex.height!=128 || c->tex.format!=D3DFMT_R5G6B5)continue;
        if(count<capacity)addresses[count]=c->desc.color;
        count++;
    }
    *total=count;return 1;
}

int d3d8_vk_rt_extent(uint32_t addr,D3D8VkRasterExtent *out)
{
    RtColor*c=rt_find(addr);if(!vk.ready||!c||!out)return 0;
    D3D8VkRasterExtent e={c->tex.width,c->tex.height,c->raster_width,c->raster_height};
    *out=e;return 1;
}

int d3d8_vk_rt_readback(uint32_t addr,void *rgba,uint32_t pitch)
{
    RtColor *c=rt_find(addr);
    Ring tmp={0};VkBufferImageCopy cp={0};uint32_t x,y;
    VkImageLayout old;
    if(!c || c->depth_only || !rgba || pitch<c->tex.width*4u) return 0;
    if(!ring_make_ex(&tmp,(VkDeviceSize)c->raster_width*c->raster_height*c->color_bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1)) return -1;
    old=c->layout;
    rt_layout(c,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    cp.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;cp.imageSubresource.layerCount=1;
    cp.imageExtent.width=c->raster_width;cp.imageExtent.height=c->raster_height;cp.imageExtent.depth=1;
    vkCmdCopyImageToBuffer(vk.cmd,c->tex.img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,tmp.buf,1,&cp);
    rt_layout(c,old);frame_submit();ring_invalidate(&tmp);
    for(y=0;y<c->tex.height;y++) for(x=0;x<c->tex.width;x++) {
        uint8_t *v=(uint8_t*)rgba+(size_t)y*pitch+(size_t)x*4u;
        uint32_t px=(uint32_t)(((uint64_t)x*2u+1u)*c->raster_width/(2u*c->tex.width));
        uint32_t py=(uint32_t)(((uint64_t)y*2u+1u)*c->raster_height/(2u*c->tex.height));
        size_t at=(size_t)py*c->raster_width+px;
        if(c->color_bytes==4) { memcpy(v,tmp.map+at*4u,4);continue; }
        uint16_t p=((uint16_t*)tmp.map)[at];
        v[0]=(uint8_t)(((p>>11)&31u)*255u/31u);v[1]=(uint8_t)(((p>>5)&63u)*255u/63u);
        v[2]=(uint8_t)((p&31u)*255u/31u);v[3]=255;
    }
    vkUnmapMemory(vk.dev,tmp.mem);vkDestroyBuffer(vk.dev,tmp.buf,NULL);vkFreeMemory(vk.dev,tmp.mem,NULL);
    return 1;
}

/* Space in a ring, flushing the frame so far when it is full. The rings are
 * sized so that a frame does not fill them; the flush is the safety net. A flush ends
 * the render pass, so callers take all their space before they bind. */
static VkDeviceSize ring_take(Ring *r, VkDeviceSize bytes, VkDeviceSize align)
{
    VkDeviceSize at = (r->at + align - 1) / align * align;
    if (at + bytes > r->size) {
        frame_submit();
        frame_begin();
        at = 0;
    }
    r->at = at + bytes;
    return at;
}

/* ── Shaders ───────────────────────────────────────────────────────────── */

/* The vertex shader's output and the pixel shader's input, as both
 * generators declare them. Kept in one place for the built-in shaders. */
#define VS_OUT_DECL \
    "struct VS_OUT {\n" \
    "    float4 pos      : SV_POSITION;\n" \
    "    float4 diffuse  : COLOR0;\n" \
    "    float4 specular : COLOR1;\n" \
    "    float4 tex0     : TEXCOORD0;\n" \
    "    float4 tex1     : TEXCOORD1;\n" \
    "    float4 tex2     : TEXCOORD2;\n" \
    "    float4 tex3     : TEXCOORD3;\n" \
    "    float  fog      : TEXCOORD4;\n" \
    "    float4 viewpos  : TEXCOORD5;\n" \
    "    float  psize    : TEXCOORD6;\n" \
    "};\n"

/* Pre-transformed vertices: the title's pixels to clip space, as the Direct3D
 * 11 backend's fixed-function shader does for FLAG_PRETRANSFORMED. The colour
 * arrives through a BGRA vertex format, so it is already in r,g,b,a order. */
static const char s_vs_fixed[] =
    "cbuffer VshFrame : register(b2) { float4 Screen; float4 TexScale; };\n"
    "struct VS_IN { float4 pos : ATTR0; float4 color : ATTR1; float2 uv : ATTR2; float fog : ATTR3; };\n"
    VS_OUT_DECL
    "VS_OUT main(VS_IN i) {\n"
    "    VS_OUT o;\n"
    "    o.pos = float4(i.pos.x * Screen.x - TexScale.w, 1.0 - i.pos.y * Screen.y, i.pos.z, 1.0);\n"
    "    if (i.pos.w != 0.0) o.pos /= i.pos.w;\n"
    "    o.diffuse = i.color;\n"
    "    o.specular = float4(0, 0, 0, 0);\n"
    "    o.tex0 = float4(i.uv, 0, 1);\n"
    "    o.tex1 = float4(0, 0, 0, 1); o.tex2 = float4(0, 0, 0, 1); o.tex3 = float4(0, 0, 0, 1);\n"
    "    o.fog = i.fog;\n"
    "    o.viewpos = float4(0, 0, 0, 1);\n"
    "    o.psize = 0.0;\n"
    "    return o;\n"
    "}\n";

/* No combiner program: stage 0 modulates the diffuse colour, or the diffuse
 * colour alone when nothing is bound there. The constants block is the
 * combiner shaders', so one buffer serves both. */
#define PS_FIXED_HEAD \
    "cbuffer CombinerCB : register(b0) {\n" \
    "    float4 c0[8]; float4 c1[8]; float4 fc0; float4 fc1; float4 fog_color;\n" \
    "    float alpha_ref; uint alpha_func; uint alpha_test_enable; uint fog_enable;\n" \
    "    uint4 alpha_only;\n" \
    "};\n" \
    "struct PS_IN {\n" \
    "    float4 pos : SV_POSITION; float4 color0 : COLOR0; float4 color1 : COLOR1;\n" \
    "    float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1; float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; float fog : TEXCOORD4;\n" \
    "};\n"
#define PS_FIXED_TAIL \
    "    if (fog_enable) result.rgb = lerp(fog_color.rgb, result.rgb, saturate(input.fog));\n" \
    "    if (alpha_test_enable) {\n" \
    "        bool pass = true;\n" \
    "        if      (alpha_func == 1u) pass = false;\n" \
    "        else if (alpha_func == 2u) pass = (result.a <  alpha_ref);\n" \
    "        else if (alpha_func == 3u) pass = (result.a == alpha_ref);\n" \
    "        else if (alpha_func == 4u) pass = (result.a <= alpha_ref);\n" \
    "        else if (alpha_func == 5u) pass = (result.a >  alpha_ref);\n" \
    "        else if (alpha_func == 6u) pass = (result.a != alpha_ref);\n" \
    "        else if (alpha_func == 7u) pass = (result.a >= alpha_ref);\n" \
    "        if (!pass) discard;\n" \
    "    }\n" \
    "    return result;\n" \
    "}\n"
static const char s_ps_tex[] =
    "Texture2D tex0 : register(t0);\nSamplerState samp0 : register(s0);\n"
    PS_FIXED_HEAD
    "float4 main(PS_IN input) : SV_TARGET {\n"
    "    float4 result = tex0.Sample(samp0, input.tc0.xy) * input.color0;\n"
    PS_FIXED_TAIL;
static const char s_ps_notex[] =
    PS_FIXED_HEAD
    "float4 main(PS_IN input) : SV_TARGET {\n"
    "    float4 result = input.color0;\n"
    PS_FIXED_TAIL;

static uint64_t usec_now(void)
{
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (uint64_t)((double)q.QuadPart * 1e6 / (double)f.QuadPart);
}

/* The module already made from this text this run, or the slot to note it in.
 * Different programs (a vertex program's hash, a combiner state) often
 * translate to the same text; they share one module, and so their pipelines. */
static ShaderSeen *shader_seen(const char *hlsl, size_t len, int vertex)
{
    uint8_t st = (uint8_t)(vertex ? 1 : 0);
    uint64_t key = vkc_hash64(hlsl, len, vkc_hash64(&st, 1, VKC_HASH_SEED));
    uint64_t check = vkc_hash64(hlsl, len, 0x9e3779b97f4a7c15ull ^ st);
    uint32_t i;

    for (i = 0; i < SEEN_SLOTS; i++) {
        ShaderSeen *e = &vk.seen[(key + i) % SEEN_SLOTS];
        if (!e->id) {
            e->key = key;
            e->check = check;
            return e;
        }
        if (e->key == key && e->check == check)
            return e;
    }
    return NULL;
}

/* Compile HLSL to a shader module, or take the SPIR-V an earlier run made from
 * the same text. Returns its id (1-based), 0 on failure. */
static uint32_t shader_make(const char *hlsl, int vertex, const char *name)
{
    shaderc_compilation_result_t res;
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    uint32_t id = 0, *cached;
    size_t len = strlen(hlsl), bytes;
    uint64_t t0 = usec_now();
    ShaderSeen *seen = shader_seen(hlsl, len, vertex);

    if (seen && seen->id) {
        vk.spv_reused++;
        return seen->id;
    }
    if (vk.shader_n >= MAX_SHADERS)
        return 0;
    cached = vkc_spirv_load(hlsl, len, vertex, &bytes);
    if (cached) {
        ci.codeSize = bytes;
        ci.pCode = cached;
        if (vkCreateShaderModule(vk.dev, &ci, NULL, &vk.shaders[vk.shader_n]) == VK_SUCCESS)
            id = ++vk.shader_n;
        free(cached);
        if (id) {
            vk.spv_hits++;
            vk.spv_load_us += usec_now() - t0;
            if (seen)
                seen->id = id;
            return id;
        }
    }
    res = shaderc_compile_into_spv(vk.sc, hlsl, len,
                                   vertex ? shaderc_vertex_shader : shaderc_fragment_shader,
                                   name, "main", vk.sc_opts);
    if (shaderc_result_get_compilation_status(res) != shaderc_compilation_status_success) {
        static int told;
        if (told++ < 6)
            LOG("%s did not compile: %s", name, shaderc_result_get_error_message(res));
    } else {
        ci.codeSize = shaderc_result_get_length(res);
        ci.pCode = (const uint32_t *)shaderc_result_get_bytes(res);
        if (vkCreateShaderModule(vk.dev, &ci, NULL, &vk.shaders[vk.shader_n]) == VK_SUCCESS) {
            id = ++vk.shader_n;
            vkc_spirv_store(hlsl, len, vertex, ci.pCode, ci.codeSize);
            if (seen)
                seen->id = id;
        }
    }
    shaderc_result_release(res);
    vk.spv_misses++;
    vk.spv_compile_us += usec_now() - t0;
    return id;
}

/* The combiner program in force as a pixel shader, 0 for none. */
static uint32_t combiner_shader(void)
{
    const NV2ACombinerState *st = d3d8_combiners_nv2a_state();
    uint32_t h = 2166136261u, i, slot;
    const uint8_t *b = (const uint8_t *)st;

    if (!st)
        return 0;
    for (i = 0; i < sizeof *st; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < COMB_SLOTS; i++) {
        CombEntry *e = &vk.combs[(h + i) % COMB_SLOTS];
        if (!e->used) {
            static char hlsl[32768];
            if (d3d8_combiners_generate_hlsl(st, hlsl, (int)sizeof hlsl) < 0)
                return 0;
            {
                /* RECOMP_COMBINER_DUMP=<n>: the first n generated shaders. */
                static int left = -1;
                if (left < 0) {
                    const char *env = getenv("RECOMP_COMBINER_DUMP");
                    left = env ? atoi(env) : 0;
                }
                if (left > 0) {
                    left--;
                    fprintf(stderr, "[COMBINER] new shader (%d stages):\n%s\n[COMBINER] end\n",
                            st->num_stages, hlsl);
                }
            }
            e->used = 1;
            e->st = *st;
            e->shader = shader_make(hlsl, 0, "ps_combiner");
            return e->shader;
        }
        if (!memcmp(&e->st, st, sizeof *st))
            return e->shader;
    }
    /* Full: reuse the home slot. A title has a few dozen of these. */
    slot = h % COMB_SLOTS;
    vk.combs[slot].used = 0;
    return combiner_shader();
}

/* ── Pipelines ─────────────────────────────────────────────────────────── */

static VkBlendFactor blend_factor(DWORD b)
{
    switch (b) {
    case D3DBLEND_ZERO:             return VK_BLEND_FACTOR_ZERO;
    case D3DBLEND_ONE:              return VK_BLEND_FACTOR_ONE;
    case D3DBLEND_SRCCOLOR:         return VK_BLEND_FACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA:         return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:        return VK_BLEND_FACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA:     return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR:        return VK_BLEND_FACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR:     return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT:      return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3DBLEND_CONSTANTCOLOR:    return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3DBLEND_INVCONSTANTCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case D3DBLEND_CONSTANTALPHA:    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case D3DBLEND_INVCONSTANTALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default:                        return VK_BLEND_FACTOR_ONE;
    }
}

/* The alpha half of a factor: the colour-named ones read alpha there. */
static VkBlendFactor blend_factor_alpha(DWORD b)
{
    switch (b) {
    case D3DBLEND_SRCALPHASAT: return VK_BLEND_FACTOR_ONE;
    default:                   return blend_factor(b);
    }
}

static VkBlendOp blend_op(DWORD op)
{
    switch (op) {
    case 2:  return VK_BLEND_OP_SUBTRACT;
    case 3:  return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 4:  return VK_BLEND_OP_MIN;
    case 5:  return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp compare_op(DWORD f)
{
    switch (f) {
    case D3DCMP_NEVER:        return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS:         return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL:        return VK_COMPARE_OP_EQUAL;
    case D3DCMP_LESSEQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3DCMP_GREATER:      return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default:                  return VK_COMPARE_OP_ALWAYS;
    }
}

static VkStencilOp stencil_op(DWORD op)
{
    switch (op) {
    case 2:  return VK_STENCIL_OP_ZERO;
    case 3:  return VK_STENCIL_OP_REPLACE;
    case 4:  return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 5:  return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 6:  return VK_STENCIL_OP_INVERT;
    case 7:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 8:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

static VkPipeline pipeline_make(const PipeKey *k)
{
    VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo st[2];
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState att;
    VkVertexInputBindingDescription bind;
    VkVertexInputAttributeDescription attr[16];
    static const VkDynamicState dyn[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_DEPTH_BIAS,
    };
    VkPipeline pipe = VK_NULL_HANDLE;
    uint32_t n = 0, a;
    uint64_t t0;

    memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vk.shaders[k->vs - 1];
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = vk.shaders[k->ps - 1];
    st[1].pName = "main";

    memset(&bind, 0, sizeof bind);
    memset(attr, 0, sizeof attr);
    bind.inputRate = k->instanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
    if (k->layout == 0) {
        bind.stride = 32;
        attr[0].location = 0; attr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT; attr[0].offset = 0;
        attr[1].location = 1; attr[1].format = VK_FORMAT_B8G8R8A8_UNORM;      attr[1].offset = 16;
        attr[2].location = 2; attr[2].format = VK_FORMAT_R32G32_SFLOAT;       attr[2].offset = 20;
        attr[3].location = 3; attr[3].format = VK_FORMAT_R32_SFLOAT; attr[3].offset = 28;
        n = 4;
    } else {
        bind.stride = (uint32_t)k->layout * 16;
        for (a = 0; a < k->layout; a++) {
            attr[a].location = a;
            attr[a].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attr[a].offset = a * 16;
        }
        n = k->layout;
    }
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = n;
    vi.pVertexAttributeDescriptions = attr;

    ia.topology = (VkPrimitiveTopology)k->topology;
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.depthBiasEnable = k->polygon_offset;
    rs.cullMode = k->cull==D3DCULL_CW ? VK_CULL_MODE_FRONT_BIT
                : k->cull==D3DCULL_CCW ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    ds.depthTestEnable = k->ztest;
    ds.depthWriteEnable = k->ztest && k->zwrite;
    ds.depthCompareOp = compare_op(k->zfunc);
    ds.stencilTestEnable = k->stencil;
    ds.front.failOp = stencil_op(k->sfail);
    ds.front.depthFailOp = stencil_op(k->szfail);
    ds.front.passOp = stencil_op(k->spass);
    ds.front.compareOp = compare_op(k->sfunc);
    ds.back = ds.front;

    memset(&att, 0, sizeof att);
    att.blendEnable = k->blend;
    att.srcColorBlendFactor = blend_factor(k->src);
    att.dstColorBlendFactor = blend_factor(k->dst);
    att.srcAlphaBlendFactor = blend_factor_alpha(k->src);
    att.dstAlphaBlendFactor = blend_factor_alpha(k->dst);
    att.colorBlendOp = att.alphaBlendOp = blend_op(k->op);
    /* D3DCOLORWRITEENABLE: red 1, green 2, blue 4, alpha 8 -- Vulkan's bits. */
    att.colorWriteMask = k->cw & 0xF;
    cb.attachmentCount = rt_active && rt_active->depth_only ? 0 : 1;
    cb.pAttachments = cb.attachmentCount ? &att : NULL;
    dy.dynamicStateCount = sizeof dyn / sizeof dyn[0];
    dy.pDynamicStates = dyn;

    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vps;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = vk.pl;
    ci.renderPass = rt_active ? rt_active->pass : vk.pass;
    t0 = usec_now();
    if (vkCreateGraphicsPipelines(vk.dev, vk.pcache, 1, &ci, NULL, &pipe) != VK_SUCCESS) {
        static int told;
        if (told++ < 6)
            LOG("pipeline creation failed (vs %u ps %u)", k->vs, k->ps);
        return VK_NULL_HANDLE;
    }
    vk.pipes_us += usec_now() - t0;
    vk.pipes_made++;
    vk.pcache_new++;
    vk.pcache_last_new = GetTickCount64();
    return pipe;
}

static VkPipeline pipeline_get(const PipeKey *k)
{
    uint32_t h = 2166136261u, i;
    const uint8_t *b = (const uint8_t *)k;

    for (i = 0; i < sizeof *k; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < PIPE_SLOTS; i++) {
        PipeEntry *e = &vk.pipes[(h + i) % PIPE_SLOTS];
        if (!e->used) {
            e->used = 1;
            e->key = *k;
            e->pipe = pipeline_make(k);
            return e->pipe;
        }
        if (!memcmp(&e->key, k, sizeof *k))
            return e->pipe;
    }
    return VK_NULL_HANDLE;
}

/* ── The cache on disk ─────────────────────────────────────────────────── */

/* The folder and seed, and the pipeline cache with what an earlier run saved
 * in it. With no folder the pipeline cache still exists, for this run only. */
static void cache_open(void)
{
    VkPipelineCacheCreateInfo ci = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    VkPhysicalDeviceProperties props;
    const char *env = getenv("RECOMP_SHADER_CACHE");
    const char *dir = env ? env : s_cache_dir;
    unsigned spv = 0, rev = 0;
    uint64_t seed;
    void *data = NULL;
    size_t size = 0;
    static const char tag[] = "d3d8_vk spirv: hlsl, io mapping, offsets, auto locations, "
                              "b0 t8 s16, performance, main";

    if (!strcmp(dir, "0") || !strcmp(dir, "off"))
        dir = "";
    shaderc_get_spv_version(&spv, &rev);
    seed = vkc_hash64(tag, sizeof tag, VKC_HASH_SEED);
    seed = vkc_hash64(&spv, sizeof spv, vkc_hash64(&rev, sizeof rev, seed));
    seed ^= SHADER_CACHE_VERSION;
    if (*dir && !vkc_open(dir, seed))
        LOG("the shader cache folder %s cannot be used; shaders are compiled every run", dir);

    vkGetPhysicalDeviceProperties(vk.phys, &props);
    memset(&vk.pcache_dev, 0, sizeof vk.pcache_dev);
    vk.pcache_dev.vendor_id = props.vendorID;
    vk.pcache_dev.device_id = props.deviceID;
    vk.pcache_dev.driver_version = props.driverVersion;
    memcpy(vk.pcache_dev.uuid, props.pipelineCacheUUID, 16);
    data = vkc_pipeline_load(&vk.pcache_dev, &size);
    vk.pcache_hash = data ? vkc_hash64(data, size, VKC_HASH_SEED) : 0;
    ci.initialDataSize = size;
    ci.pInitialData = data;
    if (vkCreatePipelineCache(vk.dev, &ci, NULL, &vk.pcache) != VK_SUCCESS) {
        ci.initialDataSize = 0;
        ci.pInitialData = NULL;
        if (vkCreatePipelineCache(vk.dev, &ci, NULL, &vk.pcache) != VK_SUCCESS)
            vk.pcache = VK_NULL_HANDLE;
        size = 0;
    }
    free(data);
    if (vkc_enabled())
        LOG("shader cache in %s (pipeline cache %s, %zu bytes)", dir,
            size ? "loaded" : "empty", size);
}

/* Write the pipeline cache when it has grown. Returns 1 when written. */
static int cache_save(const char *why)
{
    size_t size = 0;
    void *data;
    uint64_t t0 = usec_now(), h;
    int ok = 0;

    if (!vk.pcache || !vk.pcache_new || !vkc_enabled())
        return 0;
    vk.pcache_new = 0;
    if (vkGetPipelineCacheData(vk.dev, vk.pcache, &size, NULL) != VK_SUCCESS || !size)
        return 0;
    data = malloc(size);
    if (!data)
        return 0;
    if (vkGetPipelineCacheData(vk.dev, vk.pcache, &size, data) != VK_SUCCESS) {
        free(data);
        return 0;
    }
    h = vkc_hash64(data, size, VKC_HASH_SEED);
    if (h == vk.pcache_hash) {
        free(data);
        return 0;
    }
    ok = vkc_pipeline_store(&vk.pcache_dev, data, size);
    if (ok)
        vk.pcache_hash = h;
    free(data);
    LOG("pipeline cache %s (%s): %zu bytes in %.1f ms", ok ? "written" : "not written", why, size,
        (double)(usec_now() - t0) / 1000.0);
    return ok;
}

static void cache_save_at_exit(void)
{
    if (vk.ready)
        cache_save("exit");
}

/* At present: once no pipeline has been made for a few seconds, the ones made
 * before are written, so a run that is killed keeps them too. */
static void cache_tick(ULONGLONG now)
{
    if (vk.pcache_new && now - vk.pcache_last_new >= 3000)
        cache_save("idle");
}

/* The cost of shaders and pipelines so far, when it changed. */
static void cache_perf_line(void)
{
    uint32_t n = vk.spv_hits + vk.spv_misses + vk.spv_reused + vk.pipes_made;
    if (n == vk.perf_printed)
        return;
    vk.perf_printed = n;
    fprintf(stderr, "  [VKCACHE] shaders: %u from cache (%.1f ms), %u compiled (%.1f ms), %u reused; "
                    "pipelines: %u made (%.1f ms)\n",
            vk.spv_hits, (double)vk.spv_load_us / 1000.0, vk.spv_misses,
            (double)vk.spv_compile_us / 1000.0, vk.spv_reused, vk.pipes_made, (double)vk.pipes_us / 1000.0);
}

/* ── Texture formats ───────────────────────────────────────────────────── */

static uint32_t compressed_bytes(D3DFORMAT fmt, uint32_t w, uint32_t h)
{
    uint32_t bb = d3d8_format_dxt_block_bytes((uint32_t)fmt);
    return bb ? ((w + 3) / 4) * ((h + 3) / 4) * bb : 0;
}

/* The linear format that holds the same texels as a swizzled one. */
static uint32_t linear_twin(uint32_t fmt)
{
    switch (fmt) {
    case 0x00: return 0x13;     /* L8 */
    case 0x01: return 0x1B;     /* AL8 */
    case 0x02: return 0x10;     /* A1R5G5B5 */
    case 0x03: return 0x1C;     /* X1R5G5B5 */
    case 0x04: return 0x1D;     /* A4R4G4B4 */
    case 0x05: return 0x11;     /* R5G6B5 */
    case 0x06: return 0x12;     /* A8R8G8B8 */
    case 0x07: return 0x1E;     /* X8R8G8B8 */
    case 0x19: return 0x1F;     /* A8 */
    case 0x1A: return 0x20;     /* A8L8 */
    case 0x38: return 0x3D;     /* R5G5B5A1 */
    case 0x39: return 0x3E;     /* R4G4B4A4 */
    case 0x3A: return 0x3F;     /* A8B8G8R8 */
    case 0x3B: return 0x40;     /* B8G8R8A8 */
    case 0x3C: return 0x41;     /* R8G8B8A8 */
    default:   return fmt;
    }
}

static uint32_t expand(uint32_t v, uint32_t bits) { return d3d8_expand_channel(v, bits); }

/* One row of a linear format as A8R8G8B8 dwords. The values are what the
 * Direct3D 11 backend's sampler returns after its per-format fix-ups: alpha
 * only reads (1, 1, 1, a), luminance (l, l, l, 1), and so on -- so the pixel
 * shaders' alpha_only word is always 0 here. Returns 0 for an unknown format. */
static int row_to_argb(uint32_t fmt, const uint8_t *p, uint32_t w, uint32_t *out)
{
    uint32_t x, t;
    const uint16_t *p16 = (const uint16_t *)p;
    const uint32_t *p32 = (const uint32_t *)p;

    switch (fmt) {
    case 0x12: memcpy(out, p, (size_t)w * 4); return 1;
    case 0x1E: for (x = 0; x < w; x++) out[x] = p32[x] | 0xFF000000u; return 1;
    case 0x3F:
        for (x = 0; x < w; x++) {
            t = p32[x];
            out[x] = (t & 0xFF00FF00u) | ((t & 0xFF) << 16) | ((t >> 16) & 0xFF);
        }
        return 1;
    case 0x40:
        for (x = 0; x < w; x++) {
            t = p32[x];
            out[x] = ((t & 0xFFu) << 24) | (((t >> 8) & 0xFFu) << 16)
                   | (((t >> 16) & 0xFFu) << 8) | ((t >> 24) & 0xFFu);
        }
        return 1;
    case 0x41: for (x = 0; x < w; x++) { t = p32[x]; out[x] = ((t & 0xFFu) << 24) | (t >> 8); } return 1;
    case 0x10:
    case 0x1C:
        for (x = 0; x < w; x++) {
            t = p16[x];
            out[x] = ((fmt == 0x1C || (t & 0x8000u)) ? 0xFF000000u : 0u)
                   | (expand((t >> 10) & 0x1F, 5) << 16) | (expand((t >> 5) & 0x1F, 5) << 8)
                   | expand(t & 0x1F, 5);
        }
        return 1;
    case 0x11:
        for (x = 0; x < w; x++) {
            t = p16[x];
            out[x] = 0xFF000000u | (expand((t >> 11) & 0x1F, 5) << 16)
                   | (expand((t >> 5) & 0x3F, 6) << 8) | expand(t & 0x1F, 5);
        }
        return 1;
    case 0x1D:
        for (x = 0; x < w; x++) {
            t = p16[x];
            out[x] = (expand((t >> 12) & 0xF, 4) << 24) | (expand((t >> 8) & 0xF, 4) << 16)
                   | (expand((t >> 4) & 0xF, 4) << 8) | expand(t & 0xF, 4);
        }
        return 1;
    case 0x3D:                                      /* R5G5B5A1 */
        for (x = 0; x < w; x++) {
            t = p16[x];
            out[x] = ((t & 1u) ? 0xFF000000u : 0u) | (expand((t >> 11) & 0x1F, 5) << 16)
                   | (expand((t >> 6) & 0x1F, 5) << 8) | expand((t >> 1) & 0x1F, 5);
        }
        return 1;
    case 0x3E:                                      /* R4G4B4A4 */
        for (x = 0; x < w; x++) {
            t = p16[x];
            out[x] = (expand(t & 0xF, 4) << 24) | (expand((t >> 12) & 0xF, 4) << 16)
                   | (expand((t >> 8) & 0xF, 4) << 8) | expand((t >> 4) & 0xF, 4);
        }
        return 1;
    case 0x13: for (x = 0; x < w; x++) { t = p[x]; out[x] = 0xFF000000u | (t << 16) | (t << 8) | t; } return 1;
    case 0x1F: for (x = 0; x < w; x++) out[x] = ((uint32_t)p[x] << 24) | 0x00FFFFFFu; return 1;
    case 0x1B: for (x = 0; x < w; x++) { t = p[x]; out[x] = (t << 24) | (t << 16) | (t << 8) | t; } return 1;
    case 0x20:
        for (x = 0; x < w; x++) {
            t = p16[x] & 0xFF;
            out[x] = ((uint32_t)(p16[x] >> 8) << 24) | (t << 16) | (t << 8) | t;
        }
        return 1;
    case 0x24:
    case 0x25: {                                    /* YUY2, UYVY: BT.601 */
        uint32_t yoff = (fmt == 0x24) ? 0u : 1u;
        for (x = 0; x < w; x++) {
            const uint8_t *g = p + (size_t)(x & ~1u) * 2;
            int c  = (int)g[(x & 1u) ? 2 + yoff : yoff] - 16;
            int cu = (int)g[1 - yoff] - 128;
            int cv = (int)g[3 - yoff] - 128;
            int r = (298 * c + 409 * cv + 128) >> 8;
            int gg = (298 * c - 100 * cu - 208 * cv + 128) >> 8;
            int b = (298 * c + 516 * cu + 128) >> 8;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            out[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* The title's bytes as R8G8B8A8, w*h*4 bytes at dst.
 *
 * dst is the mapped staging ring, and on many drivers (RADV on the Steam Deck
 * among them) host-visible coherent memory is write-combined: writes are fast
 * and every read is an uncached trip to memory. The conversion reads what it
 * has just written, so it is done in ordinary memory and dst only receives the
 * finished image. Converting in place there took long enough per 640x480 movie
 * frame that the push-buffer executor fell behind the title, which then waited
 * for its fences: the intro movies ran at about ten frames a second and
 * stopped presenting for seconds at a time. */
static int tex_to_rgba_in(const VkTex *t, uint8_t *dst);

static int tex_to_rgba(const VkTex *t, uint8_t *dst)
{
    size_t bytes = (size_t)t->width * t->height * 4;
    uint8_t *work;
    int ok;

    /* Linear A8R8G8B8 (the texture packs' format, and movie frames): swap red and blue
     * straight into the destination. The general path below builds the image in a side
     * buffer, reorders it byte by byte and copies it again, which took 15 to 25 ms for a
     * 2048 x 2048 replacement on a Steam Deck. */
    if ((uint32_t)t->format == 0x12 && t->sys && t->width && t->pitch >= t->width * 4) {
        uint32_t x, y;
        for (y = 0; y < t->height; y++) {
            const uint8_t *s = t->sys + (size_t)y * t->pitch;
            uint32_t *d = (uint32_t *)(dst + (size_t)y * t->width * 4);
            for (x = 0; x < t->width; x++) {
                uint32_t c;
                memcpy(&c, s + (size_t)x * 4, 4);
                d[x] = (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
            }
        }
        return 1;
    }
    work = malloc(bytes ? bytes : 1);

    if (!work)
        return tex_to_rgba_in(t, dst);
    ok = tex_to_rgba_in(t, work);
    if (ok)
        memcpy(dst, work, bytes);
    free(work);
    return ok;
}

static int tex_to_rgba_in(const VkTex *t, uint8_t *dst)
{
    uint32_t fmt = (uint32_t)t->format, w = t->width, h = t->height, x, y;
    uint32_t *argb = (uint32_t *)dst;       /* converted in place, then reordered */
    const uint8_t *src = t->sys;
    uint8_t *lin = NULL;

    if (d3d8_format_dxt_block_bytes(fmt)) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                if (!d3d8_dxt_decode_texel(src, fmt, x, y, w, &argb[(size_t)y * w + x]))
                    argb[(size_t)y * w + x] = 0;
    } else {
        uint32_t bpp = t->pitch / (w ? w : 1);
        if (d3d8_format_is_swizzled(fmt)) {
            lin = malloc((size_t)t->pitch * h);
            if (!lin)
                return 0;
            xbox_unswizzle_rect(lin, src, w, h, bpp);
            src = lin;
            fmt = linear_twin(fmt);
        }
        for (y = 0; y < h; y++) {
            if (!row_to_argb(fmt, src + (size_t)y * t->pitch, w, argb + (size_t)y * w)) {
                static uint32_t told;
                if (told++ < 8)
                    LOG("texture format 0x%02X is not converted; drawn white", (unsigned)t->format);
                memset(dst, 0xFF, (size_t)w * h * 4);
                break;
            }
        }
        free(lin);
    }
    /* A8R8G8B8 dwords are B,G,R,A in memory; the image wants R,G,B,A. */
    for (y = 0; y < w * h; y++) {
        uint32_t c = argb[y];
        dst[y * 4 + 0] = (uint8_t)(c >> 16);
        dst[y * 4 + 1] = (uint8_t)(c >> 8);
        dst[y * 4 + 2] = (uint8_t)c;
        dst[y * 4 + 3] = (uint8_t)(c >> 24);
    }
    return 1;
}

/* One level of the title's bytes as R8G8B8A8: tex_to_rgba on a shallow copy that has that level's shape. */
static int convert_level(const VkTex *t, UINT level, uint8_t *dst)
{
    VkTex v = *t;
    v.width = t->width >> level ? t->width >> level : 1;
    v.height = t->height >> level ? t->height >> level : 1;
    v.sys = level ? t->lvl[level] : t->sys;
    v.pitch = level ? t->lvl_pitch[level] : t->pitch;
    return tex_to_rgba(&v, dst);
}

/* ── IDirect3DTexture8 ─────────────────────────────────────────────────── */

static HRESULT __stdcall tex_QueryInterface(IDirect3DTexture8 *s, const IID *iid, void **pp)
{ (void)s; (void)iid; (void)pp; return D3DERR_INVALIDCALL; }
static ULONG __stdcall tex_AddRef(IDirect3DTexture8 *s)
{ return (ULONG)++((VkTex *)s)->ref; }
static ULONG __stdcall tex_Release(IDirect3DTexture8 *s)
{
    VkTex *t = (VkTex *)s;
    /* Render-target aliases have a permanent cache-owner reference. A
     * GetTexture/Release pair must not free the borrowed image or this
     * embedded VkTex, even when an unrelated ready texture is evicted. */
    if (t->borrowed) {
        if (t->ref > 1) --t->ref;
        return (ULONG)t->ref;
    }
    LONG r = --t->ref;
    if (r <= 0) {
        int i;
        for (i = 0; i < STAGES; i++)
            if (vk.tex[i] == t)
                vk.tex[i] = NULL;
        retire(t->img, t->mem, t->view);
        free(t->sys);
        for (i = 1; i < (int)t->levels; i++)
            free(t->lvl[i]);
        for(unsigned f=0;f<5;f++)for(unsigned l=0;l<t->levels;l++)free(t->faces[f][l]);
        free(t);
    }
    return (ULONG)(r < 0 ? 0 : r);
}
static HRESULT __stdcall tex_GetDevice(IDirect3DTexture8 *s, IDirect3DDevice8 **pp)
{ (void)s; *pp = &g_device; return D3D_OK; }
static DWORD __stdcall tex_SetPriority(IDirect3DTexture8 *s, DWORD p) { (void)s; (void)p; return 0; }
static DWORD __stdcall tex_GetPriority(IDirect3DTexture8 *s) { (void)s; return 0; }
static void  __stdcall tex_PreLoad(IDirect3DTexture8 *s) { (void)s; }
static DWORD __stdcall tex_GetType(IDirect3DTexture8 *s) { (void)s; return 0; }
static DWORD __stdcall tex_GetLevelCount(IDirect3DTexture8 *s) { return ((VkTex *)s)->levels; }
static HRESULT __stdcall tex_GetLevelDesc(IDirect3DTexture8 *s, UINT lvl, D3DSURFACE_DESC *d)
{
    VkTex *t = (VkTex *)s;
    if (!d || lvl >= t->levels) return D3DERR_INVALIDCALL;
    memset(d, 0, sizeof *d);
    d->Format = t->format;
    d->Width = t->width >> lvl ? t->width >> lvl : 1;
    d->Height = t->height >> lvl ? t->height >> lvl : 1;
    return D3D_OK;
}
static HRESULT __stdcall tex_GetSurfaceLevel(IDirect3DTexture8 *s, UINT lvl, IDirect3DSurface8 **pp)
{ (void)s; (void)lvl; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall tex_LockRect(IDirect3DTexture8 *s, UINT lvl, D3DLOCKED_RECT *lr,
                                      const RECT *r, DWORD flags)
{
    VkTex *t = (VkTex *)s;
    (void)r; (void)flags;
    if (!lr || lvl >= t->levels || !t->sys) return D3DERR_INVALIDCALL;
    lr->Pitch = (INT)(lvl ? t->lvl_pitch[lvl] : t->pitch);
    lr->pBits = lvl ? t->lvl[lvl] : t->sys;
    return D3D_OK;
}

/* Copy one level of what the title wrote into the image. Every level of an image stays in
 * SHADER_READ_ONLY between uploads, so a level is rewritten from that state. */
static int upload_level(VkTex *t, UINT level, VkDeviceSize at)
{
    VkBufferImageCopy region;
    uint32_t lw = t->width >> level ? t->width >> level : 1, lh = t->height >> level ? t->height >> level : 1;

    image_layout_levels(vk.cmd_up, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, level, 1);
    memset(&region, 0, sizeof region);
    region.bufferOffset = at;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = lw;
    region.imageExtent.height = lh;
    region.imageExtent.depth = 1;
    if(vsj_backend_enabled()){
        VsjUploadArguments a={(uint64_t)t->img,lw,lh,level,0,vk.stage.map+at,(size_t)lw*lh*4};
        vsj_backend_event(VSJ_UPLOAD,VSJ_UPLOAD_BYTES,&a,sizeof a,0,__func__);
    }
    vkCmdCopyBufferToImage(vk.cmd_up, vk.stage.buf, t->img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    sn_touch(t->img);
    if(vsj_backend_enabled()){
        VsjUploadArguments a={(uint64_t)t->img,lw,lh,level,0,NULL,0};
        vsj_backend_event(VSJ_UPLOAD,VSJ_POST,&a,sizeof a,1,__func__);
    }
    image_layout_levels(vk.cmd_up, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, level, 1);
    return 1;
}

/* Convert what the title wrote and copy it into the image. An image a draw
 * already sampled this frame is replaced, not rewritten: the uploads of a
 * frame all run before its draws, so rewriting it would change those draws
 * too. That is a movie drawn twice in a frame, or a cache slot reused. A
 * replaced image gets every level the title had written back, from its copies. */
static HRESULT __stdcall tex_UnlockRect_vsj_body(IDirect3DTexture8 *s, UINT lvl)
{
    VkTex *t = (VkTex *)s;
    uint32_t lw, lh, other;
    VkDeviceSize bytes, at;

    if (lvl >= t->levels)
        return D3DERR_INVALIDCALL;
    lw = t->width >> lvl ? t->width >> lvl : 1;
    lh = t->height >> lvl ? t->height >> lvl : 1;
    bytes = (VkDeviceSize)lw * lh * 4;
    if (!vk.ready)
        return D3DERR_INVALIDCALL;
    frame_begin();
    if (bytes > vk.stage.size)
        return D3DERR_INVALIDCALL;
    at = ring_take(&vk.stage, bytes, 4);
    if (!convert_level(t, lvl, vk.stage.map + at))
        return D3DERR_INVALIDCALL;
    if (!t->img || t->used_frame == vk.frame) {
        retire(t->img, t->mem, t->view);
        t->img = VK_NULL_HANDLE;
        if (!image_make_levels(t->width, t->height, t->levels, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, &t->img, &t->mem, &t->view))
            return D3DERR_INVALIDCALL;
        image_layout_levels(vk.cmd_up, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, t->levels);
        t->used_frame = 0;
        /* A stage that holds this texture must pick up the new view. */
        vk.bound_set = VK_NULL_HANDLE;
        upload_level(t, lvl, at);
        for (other = 0; other < t->levels; other++) {
            uint32_t ow = t->width >> other ? t->width >> other : 1, oh = t->height >> other ? t->height >> other : 1;
            VkDeviceSize ob = (VkDeviceSize)ow * oh * 4, oat;
            if (other == lvl || !(t->have & (1u << other)) || ob > vk.stage.size)
                continue;
            oat = ring_take(&vk.stage, ob, 4);
            if (convert_level(t, other, vk.stage.map + oat))
                upload_level(t, other, oat);
        }
    } else {
        upload_level(t, lvl, at);
    }
    t->have |= 1u << lvl;
    return D3D_OK;
}
static HRESULT __stdcall tex_UnlockRect(IDirect3DTexture8*s,UINT level){
 if(vsj_backend_enabled()){
  VkTex*t=(VkTex*)s;VsjUploadArguments a={(uint64_t)t->img,t->width,t->height,level,0,NULL,0};
  vsj_backend_event(VSJ_UPLOAD,VSJ_PRE,&a,sizeof a,0,__func__);
 }
 HRESULT result=tex_UnlockRect_vsj_body(s,level);
 if(vsj_backend_enabled()){
  VkTex*t=(VkTex*)s;VsjUploadArguments a={(uint64_t)t->img,t->width,t->height,level,0,NULL,0};
  vsj_backend_event(VSJ_UPLOAD,VSJ_POST,&a,sizeof a,result,__func__);
 }
 return result;
}


static const IDirect3DTexture8Vtbl g_tex_vtbl = {
    tex_QueryInterface, tex_AddRef, tex_Release,
    tex_GetDevice, tex_SetPriority, tex_GetPriority, tex_PreLoad, tex_GetType,
    tex_GetLevelCount,
    tex_GetLevelDesc, tex_GetSurfaceLevel, tex_LockRect, tex_UnlockRect,
};

/* Six face layers with a real CubeTexture COM ABI. Own work: djultra64. */
static BYTE *cube_pixels(VkTex*t,UINT face,UINT level)
{return face?t->faces[face-1][level]:(level?t->lvl[level]:t->sys);}
static int cube_upload(VkTex*t,UINT face,UINT level)
{
 uint32_t edge=t->width>>level;if(!edge)edge=1;
 VkDeviceSize bytes=(VkDeviceSize)edge*edge*4;if(bytes>vk.stage.size)return 0;
 VkDeviceSize at=ring_take(&vk.stage,bytes,4);
 VkTex v=*t;v.width=v.height=edge;v.sys=cube_pixels(t,face,level);v.pitch=t->lvl_pitch[level];
 if(!tex_to_rgba(&v,vk.stage.map+at))return 0;
 image_layout_layers(vk.cmd_up,t->img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,level,1,face,1);
 VkBufferImageCopy r={0};r.bufferOffset=at;r.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
 r.imageSubresource.mipLevel=level;r.imageSubresource.baseArrayLayer=face;r.imageSubresource.layerCount=1;
 r.imageExtent.width=r.imageExtent.height=edge;r.imageExtent.depth=1;
 if(vsj_backend_enabled()){
  VsjUploadArguments a={(uint64_t)t->img,edge,edge,level,face,vk.stage.map+at,(size_t)bytes};
  vsj_backend_event(VSJ_UPLOAD,VSJ_UPLOAD_BYTES,&a,sizeof a,0,__func__);
 }
 vkCmdCopyBufferToImage(vk.cmd_up,vk.stage.buf,t->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
 sn_touch(t->img);
 if(vsj_backend_enabled()){
  VsjUploadArguments a={(uint64_t)t->img,edge,edge,level,face,NULL,0};
  vsj_backend_event(VSJ_UPLOAD,VSJ_POST,&a,sizeof a,1,__func__);
 }
 image_layout_layers(vk.cmd_up,t->img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,level,1,face,1);
 return 1;
}
static HRESULT __stdcall cube_QueryInterface(IDirect3DCubeTexture8*s,const IID*i,void**p){return tex_QueryInterface((IDirect3DTexture8*)s,i,p);}
static ULONG __stdcall cube_AddRef(IDirect3DCubeTexture8*s){return tex_AddRef((IDirect3DTexture8*)s);}
static ULONG __stdcall cube_Release(IDirect3DCubeTexture8*s){return tex_Release((IDirect3DTexture8*)s);}
static HRESULT __stdcall cube_GetDevice(IDirect3DCubeTexture8*s,IDirect3DDevice8**p){return tex_GetDevice((IDirect3DTexture8*)s,p);}
static DWORD __stdcall cube_SetPriority(IDirect3DCubeTexture8*s,DWORD p){return tex_SetPriority((IDirect3DTexture8*)s,p);}
static DWORD __stdcall cube_GetPriority(IDirect3DCubeTexture8*s){return tex_GetPriority((IDirect3DTexture8*)s);}
static void __stdcall cube_PreLoad(IDirect3DCubeTexture8*s){tex_PreLoad((IDirect3DTexture8*)s);}
static DWORD __stdcall cube_GetType(IDirect3DCubeTexture8*s){(void)s;return D3DRTYPE_CUBETEXTURE;}
static DWORD __stdcall cube_GetLevelCount(IDirect3DCubeTexture8*s){return tex_GetLevelCount((IDirect3DTexture8*)s);}
static HRESULT __stdcall cube_GetLevelDesc(IDirect3DCubeTexture8*s,UINT l,D3DSURFACE_DESC*d){return tex_GetLevelDesc((IDirect3DTexture8*)s,l,d);}
static HRESULT __stdcall cube_GetSurface(IDirect3DCubeTexture8*s,D3DCUBEMAP_FACES f,UINT l,IDirect3DSurface8**p)
{(void)s;(void)f;(void)l;if(p)*p=NULL;return D3DERR_INVALIDCALL;}
static HRESULT __stdcall cube_LockRect(IDirect3DCubeTexture8*s,D3DCUBEMAP_FACES face,UINT level,D3DLOCKED_RECT*r,const RECT*rect,DWORD flags)
{
 VkTex*t=(VkTex*)s;(void)flags;
 if(!r||rect||(unsigned)face>=6||level>=t->levels||!t->cube||(t->face_locked[face]&(1u<<level)))return D3DERR_INVALIDCALL;
 r->Pitch=t->lvl_pitch[level];r->pBits=cube_pixels(t,face,level);t->face_locked[face]|=1u<<level;return D3D_OK;
}
static HRESULT __stdcall cube_UnlockRect_vsj_body(IDirect3DCubeTexture8*s,D3DCUBEMAP_FACES face,UINT level)
{
 VkTex*t=(VkTex*)s;
 if((unsigned)face>=6||level>=t->levels||!t->cube||!vk.ready||!(t->face_locked[face]&(1u<<level)))return D3DERR_INVALIDCALL;
 t->face_locked[face]&=~(1u<<level);frame_begin();
 /* Uploads precede this frame's draws. Preserve the image referenced by
  * queued draws, and restore all previously written faces/levels. */
 if(!t->img||t->used_frame==vk.frame){
  retire(t->img,t->mem,t->view);t->img=VK_NULL_HANDLE;t->mem=VK_NULL_HANDLE;t->view=VK_NULL_HANDLE;
  if(!image_make_layers(t->width,t->height,t->levels,6,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,&t->img,&t->mem,&t->view))return D3DERR_INVALIDCALL;
  image_layout_layers(vk.cmd_up,t->img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,t->levels,0,6);
  t->used_frame=0;vk.bound_set=VK_NULL_HANDLE;
  for(unsigned f=0;f<6;f++)for(unsigned l=0;l<t->levels;l++)
   if((f==face&&l==level)||(t->face_have[f]&(1u<<l)))if(!cube_upload(t,f,l))return D3DERR_INVALIDCALL;
 }else if(!cube_upload(t,face,level))return D3DERR_INVALIDCALL;
 t->face_have[face]|=1u<<level;return D3D_OK;
}
static HRESULT __stdcall cube_UnlockRect(IDirect3DCubeTexture8*s,D3DCUBEMAP_FACES face,UINT level){
 if(vsj_backend_enabled()){
  VkTex*t=(VkTex*)s;VsjUploadArguments a={(uint64_t)t->img,t->width,t->height,level,(uint32_t)face,NULL,0};
  vsj_backend_event(VSJ_UPLOAD,VSJ_PRE,&a,sizeof a,0,__func__);
 }
 HRESULT result=cube_UnlockRect_vsj_body(s,face,level);
 if(vsj_backend_enabled()){
  VkTex*t=(VkTex*)s;VsjUploadArguments a={(uint64_t)t->img,t->width,t->height,level,(uint32_t)face,NULL,0};
  vsj_backend_event(VSJ_UPLOAD,VSJ_POST,&a,sizeof a,result,__func__);
 }
 return result;
}

static const IDirect3DCubeTexture8Vtbl g_cube_vtbl={cube_QueryInterface,cube_AddRef,cube_Release,cube_GetDevice,cube_SetPriority,cube_GetPriority,cube_PreLoad,cube_GetType,cube_GetLevelCount,cube_GetLevelDesc,cube_GetSurface,cube_LockRect,cube_UnlockRect};

/* ── The back buffer, for captures ─────────────────────────────────────── */

static HRESULT __stdcall sf_QueryInterface(IDirect3DSurface8 *s, const IID *iid, void **pp)
{ (void)s; (void)iid; (void)pp; return D3DERR_INVALIDCALL; }
static ULONG __stdcall sf_AddRef(IDirect3DSurface8 *s) { return (ULONG)++((VkBackSurface *)s)->ref; }
static ULONG __stdcall sf_Release(IDirect3DSurface8 *s)
{
    VkBackSurface *b = (VkBackSurface *)s;
    LONG r = --b->ref;
    if (r <= 0) { free(b->pixels); free(b); }
    return (ULONG)(r < 0 ? 0 : r);
}
static HRESULT __stdcall sf_GetDevice(IDirect3DSurface8 *s, IDirect3DDevice8 **pp)
{ (void)s; *pp = &g_device; return D3D_OK; }
static HRESULT __stdcall sf_GetDesc(IDirect3DSurface8 *s, D3DSURFACE_DESC *d)
{
    (void)s;
    if (!d) return D3DERR_INVALIDCALL;
    memset(d, 0, sizeof *d);
    d->Format = D3DFMT_A8R8G8B8; d->Width = vk.tw; d->Height = vk.th;
    return D3D_OK;
}

/* The target as it stands, R,G,B,A a pixel, at the render scale: what has
 * been drawn so far this frame is submitted and copied back. */
static HRESULT __stdcall sf_LockRect(IDirect3DSurface8 *s, D3DLOCKED_RECT *lr, const RECT *r, DWORD f)
{
    VkBackSurface *b = (VkBackSurface *)s;
    VkBufferImageCopy region;
    size_t bytes = (size_t)vk.tw * vk.th * 4;

    (void)r; (void)f;
    if (!lr || !vk.ready || bytes > vk.readback.size)
        return D3DERR_INVALIDCALL;
    frame_begin();
    pass_end();
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = vk.tw;
    region.imageExtent.height = vk.th;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(vk.cmd, vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vk.readback.buf, 1, &region);
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    frame_submit();
    if (!b->pixels)
        b->pixels = malloc(bytes);
    if (!b->pixels)
        return D3DERR_INVALIDCALL;
    ring_invalidate(&vk.readback);
    memcpy(b->pixels, vk.readback.map, bytes);
    lr->Pitch = (INT)(vk.tw * 4);
    lr->pBits = b->pixels;
    return D3D_OK;
}
static HRESULT __stdcall sf_UnlockRect(IDirect3DSurface8 *s) { (void)s; return D3D_OK; }

/* ── Asynchronous read-back (d3d8_readback.h) ──────────────────────────── */

/* The same copy as sf_LockRect, into a buffer of the slot's own, recorded in
 * the frame's command buffer and left to complete with the frame's own
 * submission. A slot is free, in flight (`after` is the count of completed
 * submits when it was recorded) or held: handed out by poll until the next
 * poll or begin. */
enum { RB_FREE, RB_FLIGHT, RB_HELD };
static struct {
    Ring     buf;
    int      state;
    uint64_t after, seq, tag;
} s_rb[D3D8_READBACK_SLOTS];
static uint64_t s_rb_seq;
static int      s_rb_broken;        /* a slot's buffer could not be made: unsupported from then on */

static void rb_release_held(void)
{
    int i;
    for (i = 0; i < D3D8_READBACK_SLOTS; i++)
        if (s_rb[i].state == RB_HELD)
            s_rb[i].state = RB_FREE;
}

int d3d8_readback_begin(uint64_t tag)
{
    VkBufferImageCopy region;
    VkDeviceSize bytes = (VkDeviceSize)vk.tw * vk.th * 4;
    int i, slot = -1;

    if (!vk.ready || s_rb_broken)
        return -1;
    rb_release_held();
    for (i = 0; i < D3D8_READBACK_SLOTS && slot < 0; i++)
        if (s_rb[i].state == RB_FREE)
            slot = i;
    if (slot < 0)
        return 0;
    if (!s_rb[slot].buf.map && !ring_make_ex(&s_rb[slot].buf, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1)) {
        s_rb_broken = 1;
        return -1;
    }
    frame_begin();
    pass_end();
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = vk.tw;
    region.imageExtent.height = vk.th;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(vk.cmd, vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s_rb[slot].buf.buf, 1, &region);
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    s_rb[slot].state = RB_FLIGHT;
    s_rb[slot].after = g_submits_done;
    s_rb[slot].seq = ++s_rb_seq;
    s_rb[slot].tag = tag;
    return 1;
}

int d3d8_readback_poll(D3D8Readback *out)
{
    int i, best = -1;

    rb_release_held();
    for (i = 0; i < D3D8_READBACK_SLOTS; i++)
        if (s_rb[i].state == RB_FLIGHT && (best < 0 || s_rb[i].seq < s_rb[best].seq))
            best = i;
    /* Copies finish in the order they were recorded: when the oldest has
     * not, none has. */
    if (best < 0 || g_submits_done <= s_rb[best].after || !out)
        return 0;
    s_rb[best].state = RB_HELD;
    ring_invalidate(&s_rb[best].buf);
    out->pixels = s_rb[best].buf.map;
    out->pitch = (int)(vk.tw * 4);
    out->width = vk.tw;
    out->height = vk.th;
    out->bgra = 0;
    out->tag = s_rb[best].tag;
    return 1;
}

void d3d8_readback_reset(void)
{
    int i, flight = 0;

    for (i = 0; i < D3D8_READBACK_SLOTS; i++)
        if (s_rb[i].state == RB_FLIGHT && g_submits_done <= s_rb[i].after)
            flight = 1;
    /* A copy still in the open command buffer names its buffer: finish it
     * before the buffer goes. */
    if (flight && vk.frame_open)
        frame_submit();
    for (i = 0; i < D3D8_READBACK_SLOTS; i++) {
        if (s_rb[i].buf.buf)
            vkDestroyBuffer(vk.dev, s_rb[i].buf.buf, NULL);
        if (s_rb[i].buf.mem)
            vkFreeMemory(vk.dev, s_rb[i].buf.mem, NULL);
        memset(&s_rb[i], 0, sizeof s_rb[i]);
    }
    s_rb_broken = 0;
}

static const IDirect3DSurface8Vtbl g_sf_vtbl = {
    sf_QueryInterface, sf_AddRef, sf_Release,
    sf_GetDevice, sf_GetDesc, sf_LockRect, sf_UnlockRect,
};

/* ── Drawing ───────────────────────────────────────────────────────────── */

#define TOPSPIN_MAX_ANISOTROPY_POLICY 1
static VkSampler sampler_get(DWORD au, DWORD av, DWORD mag, DWORD min, DWORD mip, unsigned anisotropy)
{
    static const VkSamplerAddressMode modes[4] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    };
    /* D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border; unset reads as wrap. */
    uint32_t u = (au >= 1 && au <= 4) ? au - 1 : 0, v = (av >= 1 && av <= 4) ? av - 1 : 0;
    uint32_t g = (mag == 1) ? 0 : 1, n = (min == 1) ? 0 : 1;
    /* D3DTEXF: 0 none, 1 point, 2 linear. Only a texture with a mip chain has more than level 0. */
    uint32_t m = (mip == 1 || mip == 2) ? mip : 0;
    VkSampler *s = &vk.samplers[u][v][g][n][m][anisotropy?1:0];

    if (!*s) {
        VkSamplerCreateInfo ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        ci.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE;
        ci.maxAnisotropy = anisotropy ? vk.max_anisotropy : 1.0f;
        ci.magFilter = g ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        ci.minFilter = n ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        ci.mipmapMode = m == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.maxLod = m ? VK_LOD_CLAMP_NONE : 0.0f;
        ci.addressModeU = modes[u];
        ci.addressModeV = modes[v];
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        VKC(vkCreateSampler(vk.dev, &ci, NULL, s));
    }
    return *s;
}

/* Optional quality enhancement, not a replacement for guest LOD/filter state.
 * Only RGB formats converted/supported by this backend are eligible. Unknown
 * formats, depth/YUV, cube faces and borrowed live targets retain baseline.
 * Own compatibility work: djultra64. */
static VkSampler sampler_for_texture(const VkTex *t,DWORD au,DWORD av,DWORD mag,DWORD min,DWORD mip)
{
    unsigned eligible=0;
    if(vk.max_anisotropy>1.0f && t && !t->cube && !t->borrowed && t->levels>1
       &&min==2 && mag==2 && (mip==1 || mip==2)) {
        switch(linear_twin(t->format)) {
        case 0x0C: case 0x0E: case 0x0F: /* DXT1/3/5 */
        case 0x10: case 0x11: case 0x12: case 0x1C: case 0x1D: case 0x1E:
        case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41:
            eligible=1;break;
        default:break;
        }
    }
    return sampler_get(au,av,mag,min,mip,eligible);
}

static void color_to_float(DWORD argb, float out[4])
{
    out[0] = ((argb >> 16) & 0xFF) / 255.0f;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (argb & 0xFF) / 255.0f;
    out[3] = ((argb >> 24) & 0xFF) / 255.0f;
}

/* The pixel shaders' constants for this draw, into the ring. */
static uint32_t ps_constants(void)
{
    NV2APSConstants c;
    const NV2ACombinerState *st = d3d8_combiners_nv2a_state();
    VkDeviceSize at;
    int i;

    memset(&c, 0, sizeof c);
    if (st) {
        for (i = 0; i < NV2A_MAX_COMBINER_STAGES; i++) {
            color_to_float(st->c0[i], c.c0[i]);
            color_to_float(st->c1[i], c.c1[i]);
        }
        color_to_float(st->final_c0, c.final_c0);
        color_to_float(st->final_c1, c.final_c1);
    }
    DWORD fog_argb=vk.rs[D3DRS_FOGCOLOR];float fog_data[4];
    d3d8_combiners_resolve_fog(vk.rs,fog_data,&fog_argb);
    color_to_float(fog_argb,c.fog_color);
    c.fog_enable=fog_data[3]!=0;
    c.alpha_ref = (float)(vk.rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f;
    c.alpha_func = vk.rs[D3DRS_ALPHAFUNC];
    c.alpha_test_enable = vk.rs[D3DRS_ALPHATESTENABLE] ? 1 : 0;
    at = ring_take(&vk.ubo, sizeof c, vk.ubo_align);
    memcpy(vk.ubo.map + at, &c, sizeof c);
    return (uint32_t)at;
}

static uint32_t frame_constants(float zscale, const float texscale[2])
{
    float f[12];
    VkDeviceSize at;

    /* Widescreen placement (d3d8_SetDrawPlacement): clip x becomes x * kx + bx, as the
     * epilogue computes x * Screen.x - TexScale.w; TexScale.z keeps point sprites square
     * on a widened target. All 1 (and bx 0) otherwise. */
    d3d8_combiners_resolve_fog(vk.rs,f+8,NULL);
    f[0] = 2.0f * (rt_active ? 1.0f : s_place_kx) / (float)(rt_active ? rt_active->desc.width : vk.lw);
    f[1] = 2.0f / (float)(rt_active ? rt_active->desc.height : vk.lh);
    f[2] = zscale;
    f[3] = vk.points ? vk.point_size : 0.0f;
    f[4] = texscale ? texscale[0] : 1.0f;
    f[5] = texscale ? texscale[1] : 1.0f;
    f[6] = rt_active ? 1.0f : d3d8_GetCanvasScaleY() / d3d8_GetCanvasScaleX();
    f[7] = rt_active ? 1.0f : s_place_kx - s_place_bx;
    at = ring_take(&vk.ubo, sizeof f, vk.ubo_align);
    memcpy(vk.ubo.map + at, f, sizeof f);
    return (uint32_t)at;
}

/* Memory layout is not texture-coordinate layout: block-compressed and
 * non-LIN depth formats are not Morton-swizzled either, but use normalized
 * coordinates. Only explicitly linear/pixel-addressed formats use texels. */
static BOOL texture_uses_texel_coordinates(D3DFORMAT fmt)
{
    switch ((uint32_t)fmt) {
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x18:
    case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:
    case 0x24: case 0x25: /* packed YUV has pixel coordinates */
    case 0x2E: case 0x2F: case 0x30: case 0x31:
    case 0x35: case 0x36: case 0x37:
    case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41:
    case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x60:
    case 0x61: case 0x62: case 0x63:
    case 0x67: case 0x68: case 0x69: case 0x6A: case 0x6B:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: case 0x70: case 0x71:
    case 0x72: case 0x73: case 0x74: case 0x79: case 0x7A:
        return TRUE;
    default:
        return FALSE;
    }
}

static VkRect2D draw_scissor(void)
{
    VkRect2D sc;
        if (vk.scissor[2] && vk.scissor[3]) {
            float sx = rt_active ? (float)rt_active->raster_width/rt_active->desc.width : d3d8_GetCanvasScaleX(), sy = rt_active ? (float)rt_active->raster_height/rt_active->desc.height : d3d8_GetCanvasScaleY();
            uint32_t x0 = (uint32_t)(vk.scissor[0] * sx + 0.5f), y0 = (uint32_t)(vk.scissor[1] * sy + 0.5f);
            uint32_t x1 = (uint32_t)((vk.scissor[0] + vk.scissor[2]) * sx + 0.5f);
            uint32_t y1 = (uint32_t)((vk.scissor[1] + vk.scissor[3]) * sy + 0.5f);
            uint32_t rw=rt_active?rt_active->raster_width:vk.tw,rh=rt_active?rt_active->raster_height:vk.th;
            if (x1 > rw) x1 = rw;
            if (y1 > rh) y1 = rh;
            if (x0 > x1) x0 = x1;
            if (y0 > y1) y0 = y1;
            sc.offset.x = (int32_t)x0; sc.offset.y = (int32_t)y0;
            sc.extent.width = x1 - x0; sc.extent.height = y1 - y0;
        } else {
            sc.offset.x = sc.offset.y = 0;
            sc.extent.width = rt_active?rt_active->raster_width:vk.tw;
            sc.extent.height = rt_active?rt_active->raster_height:vk.th;
        }
    return sc;
}

/* Everything but the vertex data: pipeline, descriptor set, dynamic state.
 * The caller has taken its ring space already and is inside the pass. */
static int bind_draw(uint32_t vs, uint8_t layout, uint8_t instanced, VkPrimitiveTopology topo)
{
    PipeKey k;
    VkPipeline pipe;
    VkImageView views[STAGES];
    VkSampler samplers[STAGES];
    for(unsigned st=0;st<STAGES;st++) {
        VkTex *t=vk.tex[st];
        d3d8_combiners_set_texture_info(st,t?t->format:0,t?t->width:0,t?t->height:0,
                                      2,t?t->cube:FALSE,t?texture_uses_texel_coordinates(t->format):FALSE);
    }
    int has_cube=0;
    const NV2ACombinerState*cube_state=d3d8_combiners_nv2a_state();
    for(unsigned st=0;st<STAGES;st++)if(vk.tex[st]&&vk.tex[st]->cube){
        unsigned mode=cube_state?(cube_state->texture_program>>(st*5))&31u:0;
        has_cube=1;
        if(!cube_state||(mode!=0&&mode!=3&&mode!=4&&mode!=17)){
            static unsigned reported;if(reported++<8)LOG("cube stage%u needs supported directional texture mode (got%u)",st,mode);
            return 0;
        }
    }
    uint32_t ps = combiner_shader();
    int i, same = vk.bound_set != VK_NULL_HANDLE;

    if(!ps&&has_cube){
        static unsigned reported;if(reported++<8)LOG("cube shader unavailable; refusing incompatible2D fallback");
        return 0;
    }
    if (!ps)
        ps = (vk.tex[0] && vk.tex[0]->img) ? vk.ps_tex : vk.ps_notex;
    if (!vs || !ps)
        return 0;

    memset(&k, 0, sizeof k);
    k.color_format = rt_active ? (rt_active->depth_only ? VK_FORMAT_UNDEFINED : rt_active->color_format) : VK_FORMAT_R8G8B8A8_UNORM;
    k.vs = vs; k.ps = ps; k.layout = layout; k.instanced = instanced; k.topology = (uint8_t)topo;
    k.blend = vk.rs[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
    if (k.blend) {
        k.src = (uint8_t)vk.rs[D3DRS_SRCBLEND];
        k.dst = (uint8_t)vk.rs[D3DRS_DESTBLEND];
        k.op = (uint8_t)vk.rs[D3DRS_BLENDOP];
    }
    k.cw = (uint8_t)(vk.rs[D3DRS_COLORWRITEENABLE] & 0xF);
    /* Host triangles used to expand guest points are not guest faces. */
    k.cull = vk.points ? D3DCULL_NONE : (uint8_t)vk.rs[D3DRS_CULLMODE];
    k.polygon_offset = vk.polygon_offset ? 1 : 0;
    k.has_depth = !rt_active || rt_active->depth!=NULL;
    k.ztest = k.has_depth && vk.rs[D3DRS_ZENABLE] ? 1 : 0;
    if (k.ztest) {
        k.zfunc = (uint8_t)vk.rs[D3DRS_ZFUNC];
        k.zwrite = vk.rs[D3DRS_ZWRITEENABLE] ? 1 : 0;
    }
    k.stencil = k.has_depth && vk.rs[D3DRS_STENCILENABLE] ? 1 : 0;
    if (k.stencil) {
        k.sfunc = (uint8_t)vk.rs[D3DRS_STENCILFUNC];
        k.sfail = (uint8_t)vk.rs[D3DRS_STENCILFAIL];
        k.szfail = (uint8_t)vk.rs[D3DRS_STENCILZFAIL];
        k.spass = (uint8_t)vk.rs[D3DRS_STENCILPASS];
    }
    pipe = pipeline_get(&k);
    if (!pipe)
        return 0;
    if (pipe != vk.bound_pipe) {
        vkCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vk.bound_pipe = pipe;
    }

    for (i = 0; i < STAGES; i++) {
        VkTex *t = vk.tex[i];
        if (t && t->img) {
            views[i] = sx_view(t,i);
            if(views[i]!=t->view)vsj_backend_untracked();
            t->used_frame = vk.frame;
        } else {
            const NV2ACombinerState*cs=d3d8_combiners_nv2a_state();
            unsigned mode=cs?(cs->texture_program>>(i*5))&31u:0;
            views[i]=(mode==3&&(!t||t->cube))?vk.dummy_cube_view:vk.dummy_view;
        }
        /* Depth formats need not support linear filtering. Sample stored
         * depth with point filtering; the shader applies the Xbox compare. */
        int depth_sample=t && (t->format==D3DFMT_LIN_D16 || t->format==D3DFMT_D16);
        samplers[i] = sampler_for_texture(t,vk.tss[i][13], vk.tss[i][14],
                                 depth_sample?1:vk.tss[i][16],depth_sample?1:vk.tss[i][17],
                                 depth_sample?0:vk.tss[i][18]);
        if (views[i] != vk.set_views[i] || samplers[i] != vk.set_samplers[i])
            same = 0;
    }
    if (!same) {
        VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        VkWriteDescriptorSet w[3 + 2 * STAGES];
        VkDescriptorBufferInfo bi[3];
        VkDescriptorImageInfo ii[2 * STAGES];
        VkDescriptorSet set;
        static const VkDeviceSize range[3] = { sizeof(NV2APSConstants), 192 * 16, 48 };

        ai.descriptorPool = vk.dpool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &vk.dsl;
        if (vkAllocateDescriptorSets(vk.dev, &ai, &set) != VK_SUCCESS)
            return 0;                       /* pool spent: this draw is dropped */
        memset(w, 0, sizeof w);
        memset(ii, 0, sizeof ii);
        for (i = 0; i < 3; i++) {
            bi[i].buffer = vk.ubo.buf;
            bi[i].offset = 0;
            bi[i].range = range[i];
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = (uint32_t)i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            w[i].pBufferInfo = &bi[i];
        }
        for (i = 0; i < STAGES; i++) {
            VkWriteDescriptorSet *wi = &w[3 + i], *ws = &w[3 + STAGES + i];
            ii[i].imageView = views[i];
            ii[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            wi->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wi->dstSet = set;
            wi->dstBinding = 8 + (uint32_t)i;
            wi->descriptorCount = 1;
            wi->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            wi->pImageInfo = &ii[i];
            ii[STAGES + i].sampler = samplers[i];
            ws->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws->dstSet = set;
            ws->dstBinding = 16 + (uint32_t)i;
            ws->descriptorCount = 1;
            ws->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            ws->pImageInfo = &ii[STAGES + i];
            vk.set_views[i] = views[i];
            vk.set_samplers[i] = samplers[i];
        }
        vkUpdateDescriptorSets(vk.dev, 3 + 2 * STAGES, w, 0, NULL);
        vk.bound_set = set;
    }
    vkCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pl, 0, 1,
                            &vk.bound_set, 3, vk.dyn_off);

    if (vk.scissor_dirty) {
        VkRect2D sc = draw_scissor();
        vkCmdSetScissor(vk.cmd, 0, 1, &sc);
        vk.scissor_dirty = 0;
    }
    /* NV097 polygon units are expressed in the guest zeta precision. The
     * active Z16 image is represented by D24S8 here, so preserve normalized
     * depth displacement rather than treating a Z16 unit as a D24 unit. */
    float bias_units = vk.polygon_units;
    if (rt_active && ((rt_active->desc.format >> 4) & 0xFu) == 1)
        bias_units *= 16777215.0f / 65535.0f;
    vkCmdSetDepthBias(vk.cmd, bias_units, 0.0f, vk.polygon_slope);
    vkCmdSetBlendConstants(vk.cmd, vk.blend_color);
    if (k.stencil) {
        vkCmdSetStencilReference(vk.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, vk.rs[D3DRS_STENCILREF] & 0xFF);
        vkCmdSetStencilCompareMask(vk.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, vk.rs[D3DRS_STENCILMASK] & 0xFF);
        vkCmdSetStencilWriteMask(vk.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, vk.rs[D3DRS_STENCILWRITEMASK] & 0xFF);
    }
    return 1;
}

void d3d8_SetScissorRect_vsj_body(UINT x, UINT y, UINT w, UINT h)
{
    if (vk.scissor[0] == x && vk.scissor[1] == y && vk.scissor[2] == w && vk.scissor[3] == h)
        return;
    vk.scissor[0] = x; vk.scissor[1] = y; vk.scissor[2] = w; vk.scissor[3] = h;
    vk.scissor_dirty = 1;
}
void d3d8_SetScissorRect(UINT x,UINT y,UINT w,UINT h){
 if(!vsj_backend_enabled()){d3d8_SetScissorRect_vsj_body(x,y,w,h);return;}
 uint32_t before[4];memcpy(before,vk.scissor,sizeof before);d3d8_SetScissorRect_vsj_body(x,y,w,h);
 if(memcmp(before,vk.scissor,sizeof before))vsj_backend_event(VSJ_VIEW,VSJ_POST,vk.scissor,sizeof vk.scissor,1,__func__);
}


void d3d8_vk_set_polygon_offset_vsj_body(int enabled, float factor, float units)
{
    vk.polygon_offset = enabled != 0;
    vk.polygon_slope = factor;
    vk.polygon_units = units;
}
void d3d8_vk_set_polygon_offset(int enabled,float factor,float units){
 d3d8_vk_set_polygon_offset_vsj_body(enabled,factor,units);
 if(vsj_backend_enabled()){struct{int enabled;float factor,units;}a={vk.polygon_offset,vk.polygon_slope,vk.polygon_units};vsj_backend_event(VSJ_VIEW,VSJ_POST,&a,sizeof a,1,__func__);}
}


void d3d8_SetBlendColor_vsj_body(DWORD argb)
{
    color_to_float(argb, vk.blend_color);
}
void d3d8_SetBlendColor(DWORD argb){
 d3d8_SetBlendColor_vsj_body(argb);
 if(vsj_backend_enabled())vsj_backend_event(VSJ_VIEW,VSJ_POST,vk.blend_color,sizeof vk.blend_color,1,__func__);
}


void d3d8_SetDrawPlacement(float kx,float bx){s_place_kx=kx;s_place_bx=bx;
 if(vsj_backend_enabled()){float a[2]={kx,bx};vsj_backend_event(VSJ_VIEW,VSJ_POST,a,sizeof a,1,__func__);}}

void d3d8_GetDrawPlacement(float *kx, float *bx) { *kx = s_place_kx; *bx = s_place_bx; }
/* The blurred side fill is Direct3D 11 only so far: Vulkan presents with a blit.
 * Beside a centred picture the canvas stays black here. */
void d3d8_RequestSideFill(void) { }

void d3d8_Nv2aPointMode_vsj_body(int on, float fixed_size)
{
    vk.points = on;
    vk.point_size = fixed_size;
}
void d3d8_Nv2aPointMode(int on,float size){
 d3d8_Nv2aPointMode_vsj_body(on,size);
 if(vsj_backend_enabled()){struct{int points;float size;}a={vk.points,vk.point_size};vsj_backend_event(VSJ_VIEW,VSJ_POST,&a,sizeof a,1,__func__);}
}


/* A point sprite without a geometry stage: each point is an instance, drawn
 * as a four-vertex strip whose corner comes from the vertex number. The
 * square is the size the program wrote (or SET_POINT_SIZE) in the title's
 * pixels, with the sprite's 0..1 coordinates on stage 3, as the Direct3D 11
 * backend's geometry shader makes it. */
static char *points_variant(const char *hlsl)
{
    static const char sig[] = "VS_OUT main(VS_IN i) {";
    static const char ret[] = "    return o;\n}";
    static const char tail[] =
        "    {\n"
        "        float sz = o.psize > 0.0 ? o.psize : Screen.w;\n"
        "        float2 c = float2((float)(vid & 1u), (float)((vid >> 1) & 1u));\n"
        "        if (sz <= 0.0 || o.pos.w <= 0.0) {\n"
        "            o.pos = float4(0.0, 0.0, -1.0, 1.0);\n"
        "        } else {\n"
        "            float2 h = 0.5 * sz * Screen.xy * o.pos.w * float2(TexScale.z, 1.0);\n"
        "            o.pos.x += (c.x * 2.0 - 1.0) * h.x;\n"
        "            o.pos.y -= (c.y * 2.0 - 1.0) * h.y;\n"
        "            o.tex3 = float4(c, 0, 1);\n"
        "        }\n"
        "    }\n";
    const char *a = strstr(hlsl, sig), *b = a ? strstr(a, ret) : NULL;
    size_t n;
    char *out;

    if (!a || !b)
        return NULL;
    n = strlen(hlsl) + sizeof tail + 64;
    out = malloc(n);
    if (!out)
        return NULL;
    snprintf(out, n, "%.*sVS_OUT main(VS_IN i, uint vid : SV_VertexID) {%.*s%s%s",
             (int)(a - hlsl), hlsl, (int)(b - (a + sizeof sig - 1)), a + sizeof sig - 1, tail, b);
    return out;
}

static int popcount16(uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1)
        n++;
    return n;
}

static ProgEntry *prog_find(uint32_t hash, int points)
{
    int i;
    for (i = 0; i < vk.prog_n; i++)
        if (vk.progs[i].hash == hash && vk.progs[i].points == points)
            return &vk.progs[i];
    return NULL;
}

/* Returns 1 if drawn, 0 if the program is not cached and hlsl was NULL (call
 * again with it), -1 if this program cannot be drawn here (fall back). */
int d3d8_Nv2aProgramDraw_vsj_body(uint32_t hash, const char *hlsl, uint32_t used,
                         const float *consts, uint32_t consts_version,
                         float zscale, const float texscale[2],
                         const float *verts, uint32_t nverts,
                         const uint16_t *idx, uint32_t nidx)
{
    ProgEntry *e;
    int points = vk.points, nattr = popcount16(used ? used : 1);
    VkDeviceSize stride = (VkDeviceSize)nattr * 16, vat, iat = 0;
    VkPrimitiveTopology topo;

    if (!vk.ready || !verts || !idx || !nverts || nidx < (points ? 1u : 3u))
        return vk.ready ? 1 : -1;
    e = prog_find(hash, points);
    if (!e) {
        char *alt = NULL;
        if (!hlsl)
            return 0;
        if (vk.prog_n < PROG_SLOTS) {
            e = &vk.progs[vk.prog_n++];
        } else {
            int j, old = 0;
            for (j = 1; j < PROG_SLOTS; j++)
                if (vk.progs[j].last < vk.progs[old].last)
                    old = j;
            e = &vk.progs[old];     /* its module stays; modules are not reclaimed */
        }
        memset(e, 0, sizeof *e);
        e->hash = hash;
        e->used = used;
        e->points = points;
        if (points)
            alt = points_variant(hlsl);
        e->shader = (points && !alt) ? 0 : shader_make(alt ? alt : hlsl, 1, "nv2a_vsh");
        free(alt);
        e->failed = !e->shader;
        if (!e->failed) {
            static unsigned compiled;
            if (compiled++ < 8 || (compiled & (compiled - 1)) == 0)
                LOG("compiled vertex program %08X (%d inputs%s) (#%u)", hash, nattr,
                    points ? ", points" : "", compiled);
        }
    }
    if (e->failed)
        return points ? 1 : -1;     /* points cannot fall back: skip them */
    e->last = ++vk.prog_tick;

    /* All ring space first: a full ring flushes the frame, which would end
     * the pass under a bound pipeline. */
    frame_begin();
    if (points) {
        uint32_t i;
        vat = ring_take(&vk.vtx, stride * nidx, 16);
        for (i = 0; i < nidx; i++)
            memcpy(vk.vtx.map + vat + stride * i,
                   (const uint8_t *)verts + stride * (idx[i] < nverts ? idx[i] : 0), stride);
        topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    } else {
        vat = ring_take(&vk.vtx, stride * nverts, 16);
        memcpy(vk.vtx.map + vat, verts, stride * nverts);
        iat = ring_take(&vk.idx, (VkDeviceSize)nidx * 2, 2);
        memcpy(vk.idx.map + iat, idx, (size_t)nidx * 2);
        topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    if (!vk.consts_frame_valid || consts_version != vk.consts_version) {
        VkDeviceSize at = ring_take(&vk.ubo, 192 * 16, vk.ubo_align);
        memcpy(vk.ubo.map + at, consts, 192 * 16);
        vk.consts_off = (uint32_t)at;
        vk.consts_version = consts_version;
        vk.consts_frame_valid = 1;
    }
    vk.dyn_off[0] = ps_constants();
    vk.dyn_off[2] = frame_constants(zscale, texscale);
    /* Any of those can have flushed, which forgets the constants' place. */
    if (!vk.consts_frame_valid) {
        VkDeviceSize at = ring_take(&vk.ubo, 192 * 16, vk.ubo_align);
        memcpy(vk.ubo.map + at, consts, 192 * 16);
        vk.consts_off = (uint32_t)at;
        vk.consts_frame_valid = 1;
    }
    vk.dyn_off[1] = vk.consts_off;

    pass_begin();
    if (!bind_draw(e->shader, (uint8_t)nattr, (uint8_t)points, topo))
        return 1;
    vkCmdBindVertexBuffers(vk.cmd, 0, 1, &vk.vtx.buf, &vat);
    if (points) {
        vkCmdDraw(vk.cmd, 4, nidx, 0, 0);
    } else {
        vkCmdBindIndexBuffer(vk.cmd, vk.idx.buf, iat, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(vk.cmd, nidx, 1, 0, 0, 0);
    }
    sn_targets(3);
    if(sn_accessible()){if(sn_draw_serial==UINT64_MAX)sn_versions.broken=1;else sn_draw_serial++;}
    return 1;
}
int d3d8_Nv2aProgramDraw(uint32_t hash,const char*hlsl,uint32_t used,const float*constants,uint32_t revision,float zscale,const float texscale[2],const float*verts,uint32_t nv,const uint16_t*indices,uint32_t ni){
 if(!vsj_backend_enabled())return d3d8_Nv2aProgramDraw_vsj_body(hash,hlsl,used,constants,revision,zscale,texscale,verts,nv,indices,ni);
 VsjProgramArguments a={hash,used,revision,hlsl,constants,zscale,{texscale?texscale[0]:1.0f,texscale?texscale[1]:1.0f},verts,nv,indices,ni};
 vsj_backend_event(VSJ_PROGRAM_DRAW,VSJ_PRE,&a,sizeof a,0,__func__);
 int result=d3d8_Nv2aProgramDraw_vsj_body(hash,hlsl,used,constants,revision,zscale,texscale,verts,nv,indices,ni);
 vsj_backend_event(VSJ_PROGRAM_DRAW,VSJ_POST,&a,sizeof a,result,__func__);return result;
}


/* ── IDirect3DDevice8 ──────────────────────────────────────────────────── */

static HRESULT __stdcall dev_QueryInterface(IDirect3DDevice8 *s, const IID *iid, void **pp)
{ (void)s; (void)iid; (void)pp; return D3DERR_INVALIDCALL; }
static ULONG __stdcall dev_AddRef(IDirect3DDevice8 *s) { (void)s; return 1; }
static ULONG __stdcall dev_Release(IDirect3DDevice8 *s) { (void)s; return 0; }
static HRESULT __stdcall dev_GetDirect3D(IDirect3DDevice8 *s, IDirect3D8 **pp)
{ (void)s; (void)pp; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_GetDeviceCaps(IDirect3DDevice8 *s, void *c) { (void)s; (void)c; return D3D_OK; }
static HRESULT __stdcall dev_GetDisplayMode(IDirect3DDevice8 *s, void *m) { (void)s; (void)m; return D3D_OK; }
static HRESULT __stdcall dev_GetCreationParameters(IDirect3DDevice8 *s, void *p) { (void)s; (void)p; return D3D_OK; }
static HRESULT __stdcall dev_Reset(IDirect3DDevice8 *s, D3DPRESENT_PARAMETERS *pp)
{ (void)s; (void)pp; return D3D_OK; }

static void swapchain_make(void);

/* Where the picture goes in a window of this size: all of it, or the largest
 * rectangle of the title's shape, centred. */
static void present_rect(uint32_t ww, uint32_t wh, VkOffset3D dst[2])
{
    uint32_t w = ww, h = wh;

    if (s_keep_aspect) {
        if ((uint64_t)ww * s_aspect_den > (uint64_t)wh * s_aspect_num)
            w = (uint32_t)((uint64_t)wh * s_aspect_num / s_aspect_den);
        else
            h = (uint32_t)((uint64_t)ww * s_aspect_den / s_aspect_num);
    }
    dst[0].x = (int32_t)((ww - w) / 2);
    dst[0].y = (int32_t)((wh - h) / 2);
    dst[0].z = 0;
    dst[1].x = dst[0].x + (int32_t)w;
    dst[1].y = dst[0].y + (int32_t)h;
    dst[1].z = 1;
}

/* The Direct3D 11 backend's gamma shader: each channel through the ramp. */
static const char s_gamma_vs[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);\n"
    "}\n";
static const char s_gamma_ps[] =
    "Texture2D pixels : register(t0);\n"
    "cbuffer Gamma : register(b0) { float4 ramp[256]; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "    float4 c = pixels.Load(int3(p.xy, 0));\n"
    "    uint3 i = (uint3)(saturate(c.rgb) * 255 + 0.5);\n"
    "    return float4(ramp[i.r].r, ramp[i.g].g, ramp[i.b].b, c.a);\n"
    "}\n";

/* Everything the gamma pass needs, made the first time a ramp is in force.
 * Returns 0 if it cannot be made; the window then shows the picture as drawn. */
static int gamma_make(void)
{
    static int tried;
    VkAttachmentDescription att;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    VkDescriptorSetLayoutBinding b[2];
    VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize ps[2];
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkWriteDescriptorSet w[2];
    VkDescriptorBufferInfo bi;
    VkDescriptorImageInfo ii;
    VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo st[2];
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState catt;
    static const VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    uint32_t vs, fs;

    if (vk.gamma_pipe)
        return 1;
    if (tried)
        return 0;
    tried = 1;

    if (!image_make(vk.tw, vk.th, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &vk.final, &vk.final_mem, &vk.final_view)
            || !ring_make(&vk.gamma_ubo, sizeof s_gamma, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        return 0;
    memset(&att, 0, sizeof att);
    att.format = VK_FORMAT_R8G8B8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    memset(&sub, 0, sizeof sub);
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    if (vkCreateRenderPass(vk.dev, &rpci, NULL, &vk.gamma_pass) != VK_SUCCESS)
        return 0;
    fbci.renderPass = vk.gamma_pass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &vk.final_view;
    fbci.width = vk.tw;
    fbci.height = vk.th;
    fbci.layers = 1;
    if (vkCreateFramebuffer(vk.dev, &fbci, NULL, &vk.gamma_fb) != VK_SUCCESS)
        return 0;

    /* b0 at binding 0, t0 at binding 8, as shaderc is told to place them. */
    memset(b, 0, sizeof b);
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 8;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    dlci.bindingCount = 2;
    dlci.pBindings = b;
    if (vkCreateDescriptorSetLayout(vk.dev, &dlci, NULL, &vk.gamma_dsl) != VK_SUCCESS)
        return 0;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &vk.gamma_dsl;
    if (vkCreatePipelineLayout(vk.dev, &plci, NULL, &vk.gamma_pl) != VK_SUCCESS)
        return 0;
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;  ps[1].descriptorCount = 1;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = ps;
    if (vkCreateDescriptorPool(vk.dev, &dpci, NULL, &vk.gamma_pool) != VK_SUCCESS)
        return 0;
    ai.descriptorPool = vk.gamma_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &vk.gamma_dsl;
    if (vkAllocateDescriptorSets(vk.dev, &ai, &vk.gamma_set) != VK_SUCCESS)
        return 0;
    memset(w, 0, sizeof w);
    bi.buffer = vk.gamma_ubo.buf;
    bi.offset = 0;
    bi.range = sizeof s_gamma;
    memset(&ii, 0, sizeof ii);
    ii.imageView = vk.color_view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = w[1].dstSet = vk.gamma_set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi;
    w[1].dstBinding = 8;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(vk.dev, 2, w, 0, NULL);

    vs = shader_make(s_gamma_vs, 1, "gamma_vs");
    fs = shader_make(s_gamma_ps, 0, "gamma_ps");
    if (!vs || !fs)
        return 0;
    memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vk.shaders[vs - 1];
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = vk.shaders[fs - 1];
    st[1].pName = "main";
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&catt, 0, sizeof catt);
    catt.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &catt;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vps;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = vk.gamma_pl;
    ci.renderPass = vk.gamma_pass;
    if (vkCreateGraphicsPipelines(vk.dev, vk.pcache, 1, &ci, NULL, &vk.gamma_pipe) != VK_SUCCESS) {
        vk.gamma_pipe = VK_NULL_HANDLE;
        LOG("the gamma pass could not be built; the window shows the picture without the ramp");
        return 0;
    }
    LOG("gamma ramp applied at presentation");
    return 1;
}

/* The picture through the ramp into vk.final, left ready to be copied from.
 * The main pass has ended; the colour target comes back as an attachment. */
static int gamma_draw_vsj_body(void)
{
    VkRenderPassBeginInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    VkViewport vp;
    VkRect2D sc;

    if (!s_gamma_on || !gamma_make())
        return 0;
    if (s_gamma_dirty) {
        memcpy(vk.gamma_ubo.map, s_gamma, sizeof s_gamma);
        s_gamma_dirty = 0;
    }
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    rp.renderPass = vk.gamma_pass;
    rp.framebuffer = vk.gamma_fb;
    rp.renderArea.extent.width = vk.tw;
    rp.renderArea.extent.height = vk.th;
    vkCmdBeginRenderPass(vk.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vp.x = vp.y = 0.0f;
    vp.width = (float)vk.tw; vp.height = (float)vk.th;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    sc.offset.x = sc.offset.y = 0;
    sc.extent.width = vk.tw; sc.extent.height = vk.th;
    vkCmdSetViewport(vk.cmd, 0, 1, &vp);
    vkCmdSetScissor(vk.cmd, 0, 1, &sc);
    vkCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma_pipe);
    vkCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma_pl, 0, 1,
                            &vk.gamma_set, 0, NULL);
    vkCmdDraw(vk.cmd, 3, 1, 0, 0);
    sn_touch(vk.final);
    vkCmdEndRenderPass(vk.cmd);
    image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return 1;
}
static int gamma_draw(void){
 VsjGammaArguments a={(uint32_t)s_gamma_on,vk.tw,vk.th,&s_gamma[0][0],sizeof s_gamma};
 if(vsj_backend_enabled())vsj_backend_event(VSJ_GAMMA,VSJ_PRE,&a,sizeof a,0,__func__);
 int result=gamma_draw_vsj_body();
 if(vsj_backend_enabled())vsj_backend_event(VSJ_GAMMA,VSJ_POST,&a,sizeof a,result,__func__);
 return result;
}


/* RECOMP_PRESENT_PACING=1: every two seconds, how evenly frames were
 * finished -- the line and the arithmetic of the Direct3D 11 backend's
 * d3d8_present_trace_pacing, which the test tooling reads. Without a window
 * these are frames completed, not frames shown. */
static void trace_pacing(void)
{
    static int on = -1;
    static LARGE_INTEGER freq, last, window_start;
    static double ms[512], sorted[512];
    static unsigned n;
    LARGE_INTEGER now;

    if (on < 0) {
        const char *e = getenv("RECOMP_PRESENT_PACING");
        on = e && *e == '1';
        QueryPerformanceFrequency(&freq);
    }
    if (!on)
        return;
    QueryPerformanceCounter(&now);
    if (last.QuadPart && n < 512)
        ms[n++] = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
    last = now;
    if (!window_start.QuadPart)
        window_start = now;
    if (now.QuadPart - window_start.QuadPart >= 2 * freq.QuadPart && n) {
        double lo = ms[0], hi = ms[0], sum = 0, median;
        unsigned i, j, late = 0;
        for (i = 0; i < n; i++) {
            if (ms[i] < lo) lo = ms[i];
            if (ms[i] > hi) hi = ms[i];
            sum += ms[i];
            for (j = i; j > 0 && sorted[j - 1] > ms[i]; j--) sorted[j] = sorted[j - 1];
            sorted[j] = ms[i];
        }
        median = sorted[n / 2];
        for (i = 0; i < n; i++) if (ms[i] > median * 1.5) late++;
        fprintf(stderr, "[PACING] %u presents, interval %.2f / %.2f / %.2f ms (min/mean/max), "
                        "median %.2f, %u late, sync interval %d\n",
                n, lo, sum / n, hi, median, late, 0);
        n = 0;
        window_start = now;
    }
}

/* ── The host's overlay ────────────────────────────────────────────────── */

/* What the overlay draws into, per swap-chain image. Called with the device
 * idle (swapchain_make), so nothing here is still in use. The pass outlives a
 * rebuild that keeps the format (overlay_format). */
static void overlay_release(void)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        if (vk.ov_fb[i])   vkDestroyFramebuffer(vk.dev, vk.ov_fb[i], NULL);
        if (vk.ov_view[i]) vkDestroyImageView(vk.dev, vk.ov_view[i], NULL);
        vk.ov_fb[i] = VK_NULL_HANDLE;
        vk.ov_view[i] = VK_NULL_HANDLE;
    }
}

static void overlay_format(VkFormat format)
{
    if (vk.ov_pass && vk.ov_fmt != format) {
        vkDestroyRenderPass(vk.dev, vk.ov_pass, NULL);
        vk.ov_pass = VK_NULL_HANDLE;
    }
}

/* The pass and the framebuffer for this image, made the first time they are
 * wanted. Returns 0 when they cannot be made (the frame goes without). */
static int overlay_prepare(uint32_t image)
{
    VkAttachmentDescription att;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };

    if (!vk.ov_pass) {
        memset(&att, 0, sizeof att);
        att.format = vk.swap_fmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memset(&sub, 0, sizeof sub);
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        if (vkCreateRenderPass(vk.dev, &rpci, NULL, &vk.ov_pass) != VK_SUCCESS) {
            vk.ov_pass = VK_NULL_HANDLE;
            LOG("the overlay's render pass could not be made; the window shows the picture alone");
            return 0;
        }
        vk.ov_fmt = vk.swap_fmt;
        vk.ov_gen++;
    }
    if (vk.ov_fb[image])
        return 1;
    vi.image = vk.swap_img[image];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = vk.swap_fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk.dev, &vi, NULL, &vk.ov_view[image]) != VK_SUCCESS) {
        vk.ov_view[image] = VK_NULL_HANDLE;
        return 0;
    }
    fbci.renderPass = vk.ov_pass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &vk.ov_view[image];
    fbci.width = vk.swap_ext.width;
    fbci.height = vk.swap_ext.height;
    fbci.layers = 1;
    if (vkCreateFramebuffer(vk.dev, &fbci, NULL, &vk.ov_fb[image]) != VK_SUCCESS) {
        vk.ov_fb[image] = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

/* Draw the host's overlay on the window's image, which is in transfer-destination
 * layout, and leave it ready to present. Returns 0 (image untouched) when there
 * is nothing to draw. */
static int overlay_draw(uint32_t image)
{
    VkRenderPassBeginInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    D3D8VkOverlayFrame f;

    if (!s_have_overlay || !s_overlay.active(s_overlay.user) || !overlay_prepare(image))
        return 0;
    image_layout(vk.cmd, vk.swap_img[image], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    rp.renderPass = vk.ov_pass;
    rp.framebuffer = vk.ov_fb[image];
    rp.renderArea.extent = vk.swap_ext;
    vkCmdBeginRenderPass(vk.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    memset(&f, 0, sizeof f);
    f.instance = vk.inst;
    f.physical_device = vk.phys;
    f.device = vk.dev;
    f.queue = vk.queue;
    f.queue_family = vk.qfam;
    f.command_buffer = vk.cmd;
    f.render_pass = vk.ov_pass;
    f.image_count = vk.swap_n;
    f.width = vk.swap_ext.width;
    f.height = vk.swap_ext.height;
    f.format = (int)vk.swap_fmt;
    f.generation = vk.ov_gen;
    vsj_backend_untracked();
    s_overlay.draw(&f, s_overlay.user);
    vkCmdEndRenderPass(vk.cmd);            /* leaves the image presentable */
    return 1;
}

/* RECOMP_WINDOW_SHOT=<file.bmp>: write what the window shows, overlay and all,
 * once, after the overlay has been drawn RECOMP_WINDOW_SHOT_FRAME times (20). For checking a menu on a
 * machine where nobody can take a screenshot. Returns 1 when this frame's
 * commands carry the copy. */
#define TOPSPIN_WINDOW_SHOT_SAFETY 1
static Ring     s_shot_ring;
static int      s_shot_pending, s_shot_done;
static unsigned s_shot_frames;

static int shot_record(uint32_t image)
{
    static const char *path;
    static int looked, after = 20;
    VkBufferImageCopy c;
    VkDeviceSize bytes = (VkDeviceSize)vk.swap_ext.width * vk.swap_ext.height * 4;

    if (!looked) {
        looked = 1;
        path = getenv("RECOMP_WINDOW_SHOT");
        if (path && !*path)
            path = NULL;
        if (getenv("RECOMP_WINDOW_SHOT_FRAME") && atoi(getenv("RECOMP_WINDOW_SHOT_FRAME")) > 0)
            after = atoi(getenv("RECOMP_WINDOW_SHOT_FRAME"));
    }
    if (!path || s_shot_done || (int)++s_shot_frames < after)
        return 0;
    if(!(vk.swap_usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        s_shot_done=1;LOG("RECOMP_WINDOW_SHOT: surface lacks supported transfer-source usage; capture disabled");return 0;
    }
    if(vk.swap_fmt!=VK_FORMAT_B8G8R8A8_UNORM && vk.swap_fmt!=VK_FORMAT_R8G8B8A8_UNORM
       && vk.swap_fmt!=VK_FORMAT_B8G8R8A8_SRGB && vk.swap_fmt!=VK_FORMAT_R8G8B8A8_SRGB) {
        s_shot_done=1;LOG("RECOMP_WINDOW_SHOT: unsupported swapchain pixel format %u",vk.swap_fmt);return 0;
    }
    if(!vk.swap_ext.width || !vk.swap_ext.height || vk.swap_ext.width>INT32_MAX
       || vk.swap_ext.height>INT32_MAX || bytes>64u*1024u*1024u
       || image>=vk.swap_n || image>=sizeof vk.swap_img/sizeof vk.swap_img[0]) {
        s_shot_done=1;LOG("RECOMP_WINDOW_SHOT: invalid or over-budget swapchain extent/image");return 0;
    }
    if (s_shot_ring.size < bytes) {
        if (s_shot_ring.buf) {
            vkDestroyBuffer(vk.dev, s_shot_ring.buf, NULL);
            vkFreeMemory(vk.dev, s_shot_ring.mem, NULL);
        }
        if (!ring_make_ex(&s_shot_ring, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1)) {
            s_shot_done = 1;
            return 0;
        }
    }
    image_layout(vk.cmd, vk.swap_img[image], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    memset(&c, 0, sizeof c);
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    c.imageSubresource.layerCount = 1;
    c.imageExtent.width = vk.swap_ext.width;
    c.imageExtent.height = vk.swap_ext.height;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(vk.cmd, vk.swap_img[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s_shot_ring.buf, 1, &c);
    image_layout(vk.cmd, vk.swap_img[image], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    s_shot_pending = 1;
    return 1;
}

/* After the frame has finished: the copy is in the buffer. A 32-bit BMP,
 * top-down, which every image tool reads. */
static void shot_write(void)
{
    const char *path = getenv("RECOMP_WINDOW_SHOT");
    uint32_t w = vk.swap_ext.width, h = vk.swap_ext.height, x, y;
    uint32_t pixel_bytes = w * h * 4, head[13];
    uint8_t magic[2] = { 'B', 'M' };
    int swap = vk.swap_fmt == VK_FORMAT_R8G8B8A8_UNORM || vk.swap_fmt == VK_FORMAT_R8G8B8A8_SRGB;
    int ok=1;
    FILE *f;

    s_shot_pending = 0;
    s_shot_done = 1;
    f = path ? fopen(path, "wb") : NULL;
    if (!f) {
        LOG("RECOMP_WINDOW_SHOT: cannot write %s", path ? path : "(nothing)");
        return;
    }
    ring_invalidate(&s_shot_ring);
    head[0] = 14 + 40 + pixel_bytes;            /* file size */
    head[1] = 0;
    head[2] = 14 + 40;                          /* pixel data offset */
    head[3] = 40;                               /* BITMAPINFOHEADER */
    head[4] = w;
    head[5] = (uint32_t)-(int32_t)h;            /* negative: top-down */
    head[6] = 1 | (32u << 16);                  /* planes, bits per pixel */
    head[7] = 0;                                /* BI_RGB */
    head[8] = pixel_bytes;
    head[9] = head[10] = 2835;
    head[11] = head[12] = 0;
    if(fwrite(magic, 1, 2, f)!=2 || fwrite(head, 4, 13, f)!=13)ok=0;
    for (y = 0; ok && y < h; y++) {
        const uint8_t *row = s_shot_ring.map + (size_t)y * w * 4;
        if (!swap) {
            if(fwrite(row, 4, w, f)!=w)ok=0;
        } else {
            for (x = 0; x < w; x++) {
                uint8_t px[4] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3] };
                if(fwrite(px, 1, 4, f)!=4){ok=0;break;}
            }
        }
    }
    if(fclose(f))ok=0;
    if(!ok){remove(path);LOG("RECOMP_WINDOW_SHOT: failed writing %s",path);return;}
    LOG("final swapchain GPU picture written to %s (%ux%u); after overlay, before present", path, w, h);
}

/* Display identity is independent of the last draw target. The GPU copy
 * preserves vk.color as the stable gamma/readback source. djultra64. */
static void rt_present_copy(void)
{
    RtColor *c=rt_presented;
    if(!c) return;
    VkImageLayout old=c->layout;
    VkImageBlit b={0};
    rt_layout(c,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    image_layout(vk.cmd,vk.color,VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    b.srcSubresource.aspectMask=b.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    b.srcSubresource.layerCount=b.dstSubresource.layerCount=1;
    b.srcOffsets[1]=(VkOffset3D){(int32_t)c->raster_width,(int32_t)c->raster_height,1};
    b.dstOffsets[1]=(VkOffset3D){(int32_t)vk.tw,(int32_t)vk.th,1};
    vkCmdBlitImage(vk.cmd,c->tex.img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  vk.color,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&b,VK_FILTER_LINEAR);
    sn_touch(vk.color);
    image_layout(vk.cmd,vk.color,VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    rt_layout(c,old);
}

static HRESULT __stdcall dev_Present_vsj_body(IDirect3DDevice8 *s, const RECT *src, const RECT *dst,
                                     HWND wnd, void *dirty)
{
    uint32_t image = 0;
    VkResult r;
    int have = 0, shot = 0;

    (void)s; (void)src; (void)dst; (void)wnd; (void)dirty;
    if (!vk.ready)
        return D3DERR_INVALIDCALL;
    {
        /* The line the Direct3D 11 backend prints every two seconds; the
         * unattended-run tooling reads the frame rate from it. */
        static uint32_t frames;
        static ULONGLONG last;
        ULONGLONG now = GetTickCount64();
        frames++;
        if (!last)
            last = now;
        if (now - last >= 2000) {
            LARGE_INTEGER qf;
            QueryPerformanceFrequency(&qf);
            fprintf(stderr, "  [D3D] %.1fs: %u present (%.1f fps)\n", (double)(now - last) / 1000.0,
                    frames, (double)frames * 1000.0 / (double)(now - last));
            fprintf(stderr, "  [VKPERF] a frame is %.2f ms: GPU fence %.2f, acquire %.2f, present %.2f\n",
                        (double)(now - last) / frames,
                        (double)g_fence_wait_ticks * 1000.0 / (double)qf.QuadPart / frames,
                        (double)g_acquire_ticks * 1000.0 / (double)qf.QuadPart / frames,
                        (double)g_present_ticks * 1000.0 / (double)qf.QuadPart / frames);
            g_fence_wait_ticks = g_acquire_ticks = g_present_ticks = 0;
            cache_perf_line();
            cache_tick(now);
            fflush(stderr);
            frames = 0;
            last = now;
        }
    }
    frame_begin();
    pass_end();

    rt_present_copy();
    if (scene_source_active()) {
        dpt_source_only();
        /* Continuous delayed stream owns WSI until a genuine drained cut.
         * Capture failure never substitutes an undelayed original frame. */
        scene_source_receipt();
        frame_submit_wait(VK_NULL_HANDLE); /* Original guest completion work. */
        (void)d3d8_vk_scene_wsi_maintenance();
        trace_pacing();
        return D3D_OK;
    }
    if (vk.surface && !vk.swap)
        swapchain_make();
    if (vk.swap) {
        /* A short wait: a covered or minimised window may have no image to
         * give, and the title must keep running at its own pace regardless. */
        LARGE_INTEGER qa, qb;
        int dpt_a_ok=QueryPerformanceCounter(&qa);
        r = vkAcquireNextImageKHR(vk.dev, vk.swap, 20 * 1000 * 1000ull, vk.acquired,
                                  VK_NULL_HANDLE, &image);
        int dpt_b_ok=QueryPerformanceCounter(&qb);
        dpt_result(DPT_ACQUIRE, (int32_t)r);
        dpt_ticks(DPT_ACQUIRE,dpt_a_ok&&dpt_b_ok,dpt_a_ok?qa.QuadPart:0,dpt_b_ok?qb.QuadPart:0);
        g_acquire_ticks += (uint64_t)(qb.QuadPart - qa.QuadPart);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            have = 1;
        } else if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_make();
        }
        /* VK_NOT_READY, VK_TIMEOUT: no image free; this frame is not shown. */
    }
    if (have) {
        VkImageBlit blit;
        VkClearColorValue black;
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkImage from = vk.color;
        int ramped = gamma_draw();

        memset(&black, 0, sizeof black);
        black.float32[3] = 1.0f;
        if (ramped)
            from = vk.final;                /* already a transfer source */
        else
            image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        image_layout(vk.cmd, vk.swap_img[image], VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(vk.cmd, vk.swap_img[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &black, 1, &range);
        /* Final swapchain WAW: command order alone does not make the clear
         * visible before the overlapping blit. Own integration: djultra64. */
        VkImageMemoryBarrier clear_to_blit = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk.swap_img[image],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        vkCmdPipelineBarrier(vk.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, NULL, 0, NULL, 1, &clear_to_blit);
        memset(&blit, 0, sizeof blit);
        blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[1].x = (int32_t)vk.tw;
        blit.srcOffsets[1].y = (int32_t)vk.th;
        blit.srcOffsets[1].z = 1;
        present_rect(vk.swap_ext.width, vk.swap_ext.height, blit.dstOffsets);
        vkCmdBlitImage(vk.cmd, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vk.swap_img[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       s_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
        if (overlay_draw(image))
            shot = shot_record(image);
        else
            image_layout(vk.cmd, vk.swap_img[image], VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        if (!ramped)
            image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    frame_submit_wait(have ? vk.acquired : VK_NULL_HANDLE);
    if (shot && s_shot_pending)
        shot_write();
    if (have) {
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.swapchainCount = 1;
        pi.pSwapchains = &vk.swap;
        pi.pImageIndices = &image;
        LARGE_INTEGER qa, qb;
        int dpt_a_ok=QueryPerformanceCounter(&qa);
        r = vkQueuePresentKHR(vk.queue, &pi);
        int dpt_b_ok=QueryPerformanceCounter(&qb);
        dpt_result(DPT_QUEUE_PRESENT, (int32_t)r);
        dpt_ticks(DPT_QUEUE_PRESENT,dpt_a_ok&&dpt_b_ok,dpt_a_ok?qa.QuadPart:0,dpt_b_ok?qb.QuadPart:0);
        g_present_ticks += (uint64_t)(qb.QuadPart - qa.QuadPart);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            swapchain_make();
        trace_pacing();
    } else if (vk.swap && s_have_host && s_host.drawable_size) {
        /* A window that changed size without the swap chain saying so. */
        int w = 0, h = 0;
        s_host.drawable_size(&w, &h, s_host.user);
        if (w > 0 && h > 0 && ((uint32_t)w != vk.swap_ext.width || (uint32_t)h != vk.swap_ext.height))
            swapchain_make();
    }
    if (!have)
        trace_pacing();
    /* Original safe boundary: after the original wait and physical Present. */
    (void)d3d8_vk_scene_wsi_maintenance();
    return D3D_OK;
}
static HRESULT __stdcall dev_Present(IDirect3DDevice8*s,const RECT*src,const RECT*dst,HWND window,void*dirty){
 dpt_begin_present();
 vsj_backend_boundary(VSJ_HOST_ENTER,0,0);
 HRESULT result=dev_Present_vsj_body(s,src,dst,window,dirty);
 dpt_end_present((int32_t)result);
 vsj_backend_boundary(VSJ_HOST_LEAVE,0,0);return result;
}


static HRESULT __stdcall dev_GetBackBuffer(IDirect3DDevice8 *s, INT i, DWORD t, IDirect3DSurface8 **pp)
{
    VkBackSurface *b;
    (void)s; (void)i; (void)t;
    if (!pp || !vk.ready)
        return D3DERR_INVALIDCALL;
    b = calloc(1, sizeof *b);
    if (!b)
        return D3DERR_INVALIDCALL;
    b->iface.lpVtbl = &g_sf_vtbl;
    b->ref = 1;
    *pp = &b->iface;
    return D3D_OK;
}

static HRESULT __stdcall dev_BeginScene(IDirect3DDevice8 *s) { (void)s; return D3D_OK; }
static HRESULT __stdcall dev_EndScene(IDirect3DDevice8 *s) { (void)s; return D3D_OK; }

/* The whole target, whatever the scissor and the colour mask say, as the
 * Direct3D 11 backend clears it. */
static HRESULT __stdcall dev_Clear_vsj_body(IDirect3DDevice8 *s, DWORD count, const D3DRECT *rects,
                                   DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    VkClearAttachment att[2];
    VkClearRect rect;
    uint32_t n = 0;

    (void)s;
    if (!vk.ready || (count && !rects)
        || (count && rt_active && (!rt_active->desc.width || !rt_active->desc.height)))
        return D3DERR_INVALIDCALL;
    for (DWORD i=0;i<count;i++)
        if (rects[i].x2<rects[i].x1 || rects[i].y2<rects[i].y1)
            return D3DERR_INVALIDCALL;
    memset(att, 0, sizeof att);
    if ((flags & D3DCLEAR_TARGET) && !(rt_active && rt_active->depth_only)) {
        att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        color_to_float(color, att[n].clearValue.color.float32);
        n++;
    }
    if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) && (!rt_active || rt_active->depth)) {
        if (flags & D3DCLEAR_ZBUFFER) att[n].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (flags & D3DCLEAR_STENCIL) att[n].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        att[n].clearValue.depthStencil.depth = z;
        att[n].clearValue.depthStencil.stencil = stencil & 0xFF;
        n++;
    }
    if (!n)
        return D3D_OK;
    pass_begin();
    memset(&rect, 0, sizeof rect);
    rect.rect.extent.width = rt_active?rt_active->raster_width:vk.tw;
    rect.rect.extent.height = rt_active?rt_active->raster_height:vk.th;
    rect.layerCount = 1;
    if (!count) {
        vkCmdClearAttachments(vk.cmd, n, att, 1, &rect);
        sn_targets(((flags&D3DCLEAR_TARGET)?1u:0)|((flags&(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL))?2u:0));
    } else {
        /* NV097 clear rectangles use raster pixels, as surface clip does.
         * Expand to the selected attachment's AA backing dimensions exactly
         * once; explicit rectangles remain independent of draw scissor.
         * Inclusive NV endpoints were already converted to exclusive D3DRECT
         * endpoints by PGRAPH. Host-main API keeps its canvas scaling.
         * Own compatibility work: djultra64. */
        const double sx=rt_active?(double)rt_active->raster_width/rt_active->desc.width:d3d8_GetCanvasScaleX();
        const double sy=rt_active?(double)rt_active->raster_height/rt_active->desc.height:d3d8_GetCanvasScaleY();
        const uint32_t width=rect.rect.extent.width,height=rect.rect.extent.height;
        for (DWORD i=0;i<count;i++) {
            double x0=rects[i].x1*sx,y0=rects[i].y1*sy;
            double x1=rects[i].x2*sx,y1=rects[i].y2*sy;
            if(x0<0)x0=0;if(y0<0)y0=0;
            if(x1<0)x1=0;if(y1<0)y1=0;
            if(x0>width)x0=width;if(y0>height)y0=height;
            if(x1>width)x1=width;if(y1>height)y1=height;
            uint32_t left=(uint32_t)(x0+0.5),top=(uint32_t)(y0+0.5);
            uint32_t right=(uint32_t)(x1+0.5),bottom=(uint32_t)(y1+0.5);
            if(left>=right || top>=bottom)continue;
            rect.rect.offset=(VkOffset2D){(int32_t)left,(int32_t)top};
            rect.rect.extent=(VkExtent2D){right-left,bottom-top};
            vkCmdClearAttachments(vk.cmd,n,att,1,&rect);
            sn_targets(((flags&D3DCLEAR_TARGET)?1u:0)|((flags&(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL))?2u:0));
        }
    }
    return D3D_OK;
}
static HRESULT __stdcall dev_Clear(IDirect3DDevice8*s,DWORD count,const D3DRECT*rects,DWORD flags,D3DCOLOR color,float depth,DWORD stencil){
 if(!vsj_backend_enabled())return dev_Clear_vsj_body(s,count,rects,flags,color,depth,stencil);
 VsjClearArguments a={count,flags,color,stencil,depth,rects};vsj_backend_event(VSJ_CLEAR,VSJ_PRE,&a,sizeof a,0,__func__);
 HRESULT result=dev_Clear_vsj_body(s,count,rects,flags,color,depth,stencil);
 vsj_backend_event(VSJ_CLEAR,VSJ_POST,&a,sizeof a,result,__func__);return result;
}


static HRESULT __stdcall dev_SetTransform(IDirect3DDevice8 *s, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{ (void)s; (void)st; (void)m; return D3D_OK; }
static HRESULT __stdcall dev_GetTransform(IDirect3DDevice8 *s, D3DTRANSFORMSTATETYPE st, D3DMATRIX *m)
{ (void)s; (void)st; if (m) memset(m, 0, sizeof *m); return D3D_OK; }

static HRESULT __stdcall dev_SetRenderState(IDirect3DDevice8 *s, D3DRENDERSTATETYPE st, DWORD v)
{
    (void)s;
    if ((DWORD)st < MAX_RS)
        vk.rs[(DWORD)st] = v;
    return D3D_OK;
}
static HRESULT __stdcall dev_GetRenderState(IDirect3DDevice8 *s, D3DRENDERSTATETYPE st, DWORD *pv)
{
    (void)s;
    if (!pv || (DWORD)st >= MAX_RS) return D3DERR_INVALIDCALL;
    *pv = vk.rs[(DWORD)st];
    return D3D_OK;
}
static HRESULT __stdcall dev_SetTextureStageState(IDirect3DDevice8 *s, DWORD stage,
                                                  D3DTEXTURESTAGESTATETYPE type, DWORD v)
{
    (void)s;
    if (stage < STAGES && (DWORD)type < 32)
        vk.tss[stage][(DWORD)type] = v;
    return D3D_OK;
}
static HRESULT __stdcall dev_GetTextureStageState(IDirect3DDevice8 *s, DWORD stage,
                                                  D3DTEXTURESTAGESTATETYPE type, DWORD *pv)
{
    (void)s;
    if (!pv || stage >= STAGES || (DWORD)type >= 32) return D3DERR_INVALIDCALL;
    *pv = vk.tss[stage][(DWORD)type];
    return D3D_OK;
}
static HRESULT __stdcall dev_SetTexture(IDirect3DDevice8 *s, DWORD stage, IDirect3DBaseTexture8 *t)
{
    (void)s;
    if (stage < STAGES)
        vk.tex[stage] = (VkTex *)t;
    return D3D_OK;
}
static HRESULT __stdcall dev_GetTexture(IDirect3DDevice8 *s, DWORD stage, IDirect3DBaseTexture8 **pp)
{
    (void)s;
    if (!pp || stage >= STAGES) return D3DERR_INVALIDCALL;
    *pp = (IDirect3DBaseTexture8 *)vk.tex[stage];
    if (vk.tex[stage])
        vk.tex[stage]->ref++;
    return D3D_OK;
}
static HRESULT __stdcall dev_SetStreamSource(IDirect3DDevice8 *s, UINT n, IDirect3DVertexBuffer8 *vb, UINT stride)
{ (void)s; (void)n; (void)vb; (void)stride; return D3D_OK; }
static HRESULT __stdcall dev_GetStreamSource(IDirect3DDevice8 *s, UINT n, IDirect3DVertexBuffer8 **pp, UINT *stride)
{ (void)s; (void)n; if (pp) *pp = NULL; if (stride) *stride = 0; return D3D_OK; }
static HRESULT __stdcall dev_SetIndices(IDirect3DDevice8 *s, IDirect3DIndexBuffer8 *ib, UINT base)
{ (void)s; (void)ib; (void)base; return D3D_OK; }
static HRESULT __stdcall dev_GetIndices(IDirect3DDevice8 *s, IDirect3DIndexBuffer8 **pp, UINT *base)
{ (void)s; if (pp) *pp = NULL; if (base) *base = 0; return D3D_OK; }

static HRESULT __stdcall dev_DrawPrimitive(IDirect3DDevice8 *s, D3DPRIMITIVETYPE pt, UINT start, UINT count)
{ (void)s; (void)pt; (void)start; (void)count; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_DrawIndexedPrimitive(IDirect3DDevice8 *s, D3DPRIMITIVETYPE pt,
                                                  UINT minv, UINT nv, UINT start, UINT count)
{ (void)s; (void)pt; (void)minv; (void)nv; (void)start; (void)count; return D3DERR_INVALIDCALL; }

/* Exact state used by fixed draws, identified by the bound YUV texture.
 * The diagnostic never starts a pass or reads GPU pixels. djultra64. */
#define TOPSPIN_MOVIE_TRACE_DIAGNOSTIC 1
static void movie_trace_fixed(const void *data, unsigned stride, unsigned count)
{
    static int enabled=-1;
    typedef struct {uint32_t dims[10],rs[MAX_RS],tss[32];VkRect2D sc;float bounds[8];} Tuple;
    static Tuple seen[16];static unsigned shown;
    const VkTex *tex=vk.tex[0];Tuple t;unsigned i,j,finite=0;
    if(enabled<0){const char*e=getenv("TOPSPIN_MOVIE_TRACE");enabled=e&&!strcmp(e,"1");}
    if(!enabled||shown==16||!tex||(tex->format!=D3DFMT_YUY2&&tex->format!=D3DFMT_UYVY)||!data||!count)return;
    memset(&t,0,sizeof t);t.sc=draw_scissor();
    uint32_t dims[10]={rt_active?rt_active->desc.width:0,rt_active?rt_active->desc.height:0,
        rt_active?rt_active->raster_width:vk.tw,rt_active?rt_active->raster_height:vk.th,
        rt_active?rt_active->desc.format:0,tex->format,tex->width,tex->height,tex->pitch,count};
    memcpy(t.dims,dims,sizeof dims);memcpy(t.rs,vk.rs,sizeof t.rs);memcpy(t.tss,vk.tss[0],sizeof t.tss);
    for(i=0;i<count;i++){
        float p[7];memcpy(p,(const uint8_t*)data+(size_t)i*stride,28);
        const float a[4]={p[0],p[1],p[5],p[6]};
        if(!isfinite(a[0])||!isfinite(a[1])||!isfinite(a[2])||!isfinite(a[3]))continue;
        for(j=0;j<4;j++){if(!finite||a[j]<t.bounds[j])t.bounds[j]=a[j];if(!finite||a[j]>t.bounds[j+4])t.bounds[j+4]=a[j];}finite++;
    }
    for(i=0;i<shown;i++)if(!memcmp(&seen[i],&t,sizeof t))return;
    seen[shown++]=t;
    fprintf(stderr,"[MOVIE-VK] tuple=%u hostFrame=%llu target=%08X zeta=%08X fmt=%08X pitch=%u/%u logical=%ux%u physical=%ux%u viewport=0,%u,%u,-%u scissor=%ux%u+%d+%d rawScissor=%ux%u+%u+%u vertices=%u finite=%u bbox=%g,%g..%g,%g uv=%g,%g..%g,%g texfmt=%02X size=%ux%u pitch=%u depth=%u/%u/%u stencil=%u/%u/%u alpha=%u/%u/%u blend=%u/%u/%u mask=%u cull=%u\n",
        shown,(unsigned long long)vk.frame,rt_active?rt_active->desc.color:0,rt_active?rt_active->desc.zeta:0,t.dims[4],rt_active?rt_active->desc.color_pitch:0,rt_active?rt_active->desc.zeta_pitch:0,
        t.dims[0],t.dims[1],t.dims[2],t.dims[3],t.dims[3],t.dims[2],t.dims[3],t.sc.extent.width,t.sc.extent.height,t.sc.offset.x,t.sc.offset.y,
        vk.scissor[2],vk.scissor[3],vk.scissor[0],vk.scissor[1],count,finite,t.bounds[0],t.bounds[1],t.bounds[4],t.bounds[5],t.bounds[2],t.bounds[3],t.bounds[6],t.bounds[7],
        tex->format,tex->width,tex->height,tex->pitch,vk.rs[D3DRS_ZENABLE],vk.rs[D3DRS_ZFUNC],vk.rs[D3DRS_ZWRITEENABLE],vk.rs[D3DRS_STENCILENABLE],vk.rs[D3DRS_STENCILFUNC],vk.rs[D3DRS_STENCILREF],
        vk.rs[D3DRS_ALPHATESTENABLE],vk.rs[D3DRS_ALPHAFUNC],vk.rs[D3DRS_ALPHAREF],vk.rs[D3DRS_ALPHABLENDENABLE],vk.rs[D3DRS_SRCBLEND],vk.rs[D3DRS_DESTBLEND],vk.rs[D3DRS_COLORWRITEENABLE],vk.rs[D3DRS_CULLMODE]);
    fprintf(stderr,"  movie-vk tss0=");for(i=0;i<32;i++)fprintf(stderr,"%s%08X",i?",":"",t.tss[i]);fprintf(stderr,"\n");
}

/* Pre-transformed vertices, 28 bytes each: x, y, z, rhw, colour, u, v. */
static HRESULT __stdcall dev_DrawPrimitiveUP_vsj_body(IDirect3DDevice8 *s, D3DPRIMITIVETYPE pt,
                                             UINT prims, const void *data, UINT stride)
{
    VkPrimitiveTopology topo;
    uint32_t count, i;
    VkDeviceSize vat;
    const uint8_t *src = data;

    (void)s;
    if (!vk.ready || !data || (stride != 28 && stride != 32) || !prims)
        return D3DERR_INVALIDCALL;
    switch (pt) {
    case D3DPT_POINTLIST:     topo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;     count = prims; break;
    case D3DPT_LINELIST:      topo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;      count = prims * 2; break;
    case D3DPT_LINESTRIP:     topo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;     count = prims + 1; break;
    case D3DPT_TRIANGLELIST:  topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  count = prims * 3; break;
    case D3DPT_TRIANGLESTRIP: topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; count = prims + 2; break;
    case D3DPT_TRIANGLEFAN:   topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  count = prims * 3; break;
    default: return D3DERR_INVALIDCALL;
    }

    movie_trace_fixed(data,stride,pt==D3DPT_TRIANGLEFAN?prims+2:count);
    frame_begin();
    vat = ring_take(&vk.vtx, (VkDeviceSize)count * 32, 4);
    float fog_data[4];d3d8_combiners_resolve_fog(vk.rs,fog_data,NULL);
    for(i=0;i<count;i++) {
        uint32_t source=i;
        if(pt==D3DPT_TRIANGLEFAN)source=i%3==0?0:i/3+(i%3);
        const uint8_t *v=src+(size_t)source*stride;
        uint8_t *dst=vk.vtx.map+vat+(size_t)i*32;
        memcpy(dst,v,28);
        float factor;
        if(stride==32)memcpy(&factor,v+28,4);
        else {float z;memcpy(&z,v+8,4);factor=d3d8_nv_fog_factor(z,(int)fog_data[3],(unsigned)fog_data[2],fog_data[0],fog_data[1]);}
        memcpy(dst+28,&factor,4);
    }
    vk.dyn_off[0] = ps_constants();
    vk.dyn_off[2] = frame_constants(1.0f, NULL);
    vk.dyn_off[1] = 0;                      /* unread by this vertex shader */

    pass_begin();
    if (!bind_draw(vk.vs_fixed, 0, 0, topo))
        return D3DERR_INVALIDCALL;
    vkCmdBindVertexBuffers(vk.cmd, 0, 1, &vk.vtx.buf, &vat);
    if(vsj_backend_enabled()){
        VsjGeometryArguments a={vk.vtx.map+vat,(size_t)count*32,32,count,VSJ_GEOMETRY_FIXED_GPU32};
        vsj_backend_event(VSJ_UP_DRAW,VSJ_GEOMETRY,&a,sizeof a,0,__func__);
    }
    vkCmdDraw(vk.cmd, count, 1, 0, 0);
    sn_targets(3);
    if(sn_accessible()){if(sn_draw_serial==UINT64_MAX)sn_versions.broken=1;else sn_draw_serial++;}
    return D3D_OK;
}
static HRESULT __stdcall dev_DrawPrimitiveUP(IDirect3DDevice8*s,D3DPRIMITIVETYPE pt,UINT prims,const void*vertices,UINT stride){
 if(!vsj_backend_enabled())return dev_DrawPrimitiveUP_vsj_body(s,pt,prims,vertices,stride);
 VsjUpArguments a={pt,prims,stride,vertices};vsj_backend_event(VSJ_UP_DRAW,VSJ_PRE,&a,sizeof a,0,__func__);
 HRESULT result=dev_DrawPrimitiveUP_vsj_body(s,pt,prims,vertices,stride);
 vsj_backend_event(VSJ_UP_DRAW,VSJ_POST,&a,sizeof a,result,__func__);return result;
}


static HRESULT __stdcall dev_DrawIndexedPrimitiveUP(IDirect3DDevice8 *s, D3DPRIMITIVETYPE pt,
        UINT minv, UINT nv, UINT prims, const void *idx, D3DFORMAT ifmt, const void *data, UINT stride)
{ (void)s; (void)pt; (void)minv; (void)nv; (void)prims; (void)idx; (void)ifmt; (void)data; (void)stride;
  return D3DERR_INVALIDCALL; }

static HRESULT __stdcall dev_CreateTexture(IDirect3DDevice8 *s, UINT w, UINT h, UINT levels,
        DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **pp)
{
    VkTex *t;
    uint32_t cb = compressed_bytes(fmt, w, h), longest, most = 1, l;

    (void)s; (void)usage; (void)pool;
    if (!pp || !w || !h || w > 4096 || h > 4096)
        return D3DERR_INVALIDCALL;
    /* One level unless a full chain is asked for (the title makes none; texture packs do). */
    for (longest = w > h ? w : h; longest > 1; longest >>= 1)
        most++;
    if (levels == 0 || levels > MAX_TEX_LEVELS)
        levels = levels ? MAX_TEX_LEVELS : 1;
    if (levels > most)
        levels = most;
    t = calloc(1, sizeof *t);
    if (!t)
        return D3DERR_INVALIDCALL;
    t->iface.lpVtbl = &g_tex_vtbl;
    t->ref = 1;
    t->width = w; t->height = h; t->format = fmt; t->levels = levels;
    if (cb) {
        t->pitch = cb / ((h + 3) / 4);
        t->sys_bytes = cb;
    } else {
        UINT bpp = d3d8_format_bpp(fmt) / 8;
        t->pitch = w * (bpp ? bpp : 4);
        t->sys_bytes = t->pitch * h;
    }
    t->sys = calloc(1, t->sys_bytes);
    if (!t->sys) {
        free(t);
        return D3DERR_INVALIDCALL;
    }
    t->lvl[0] = t->sys;
    t->lvl_pitch[0] = t->pitch;
    for (l = 1; l < levels; l++) {
        uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1, lcb = compressed_bytes(fmt, lw, lh), bytes;
        UINT bpp = d3d8_format_bpp(fmt) / 8;
        t->lvl_pitch[l] = lcb ? lcb / ((lh + 3) / 4) : lw * (bpp ? bpp : 4);
        bytes = lcb ? lcb : t->lvl_pitch[l] * lh;
        t->lvl[l] = calloc(1, bytes);
        if (!t->lvl[l]) {
            while (l-- > 1)
                free(t->lvl[l]);
            free(t->sys);
            free(t);
            return D3DERR_INVALIDCALL;
        }
    }
    *pp = &t->iface;
    return D3D_OK;
}

/* Available through the SDK's appended CreateCubeTexture slot too. */
int32_t d3d8_vk_create_cube_texture(uint32_t edge,uint32_t levels,uint32_t format,IDirect3DCubeTexture8**out)
{
 if(!out)return D3DERR_INVALIDCALL;*out=NULL;
 if(!vk.ready||!edge||edge>4096||(edge&(edge-1))||!levels||levels>MAX_TEX_LEVELS||format==0x0b||(!compressed_bytes(format,edge,edge)&&!d3d8_format_is_swizzled(format)))return D3DERR_INVALIDCALL;
 uint32_t max=1;for(uint32_t e=edge;e>1;e>>=1)max++;
 if(levels>max||(uint64_t)edge*edge*4>vk.stage.size)return D3DERR_INVALIDCALL;
 VkPhysicalDeviceProperties props;VkImageFormatProperties ip;vkGetPhysicalDeviceProperties(vk.phys,&props);
 if(edge>props.limits.maxImageDimensionCube||vkGetPhysicalDeviceImageFormatProperties(vk.phys,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,&ip)!=VK_SUCCESS||ip.maxArrayLayers<6||edge>ip.maxExtent.width||levels>ip.maxMipLevels)return D3DERR_INVALIDCALL;
 IDirect3DTexture8*base=NULL;HRESULT hr=dev_CreateTexture(&g_device,edge,edge,levels,0,(D3DFORMAT)format,0,&base);if(FAILED(hr))return hr;
 VkTex*t=(VkTex*)base;t->cube=1;
 for(unsigned f=0;f<5;f++)for(unsigned l=0;l<levels;l++){
  uint32_t w=edge>>l;if(!w)w=1;uint32_t bytes=compressed_bytes(format,w,w);if(!bytes)bytes=t->lvl_pitch[l]*w;
  t->faces[f][l]=calloc(1,bytes);if(!t->faces[f][l]){tex_Release(base);return D3DERR_INVALIDCALL;}
 }
 t->iface.lpVtbl=(const IDirect3DTexture8Vtbl*)&g_cube_vtbl;*out=(IDirect3DCubeTexture8*)t;return D3D_OK;
}
static HRESULT __stdcall dev_CreateCubeTexture(IDirect3DDevice8*s,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DCubeTexture8**out)
{(void)s;(void)usage;(void)pool;return d3d8_vk_create_cube_texture(edge,levels,format,out);}

static HRESULT __stdcall dev_CreateVertexBuffer(IDirect3DDevice8 *s, UINT len, DWORD usage,
        DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **pp)
{ (void)s; (void)len; (void)usage; (void)fvf; (void)pool; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_CreateIndexBuffer(IDirect3DDevice8 *s, UINT len, DWORD usage,
        D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **pp)
{ (void)s; (void)len; (void)usage; (void)fmt; (void)pool; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_CreateRenderTarget(IDirect3DDevice8 *s, UINT w, UINT h,
        D3DFORMAT fmt, DWORD ms, BOOL lockable, IDirect3DSurface8 **pp)
{ (void)s; (void)w; (void)h; (void)fmt; (void)ms; (void)lockable; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_CreateDepthStencilSurface(IDirect3DDevice8 *s, UINT w, UINT h,
        D3DFORMAT fmt, DWORD ms, IDirect3DSurface8 **pp)
{ (void)s; (void)w; (void)h; (void)fmt; (void)ms; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_SetRenderTarget(IDirect3DDevice8 *s, IDirect3DSurface8 *rt, IDirect3DSurface8 *ds)
{ (void)s; (void)rt; (void)ds; return D3D_OK; }
static HRESULT __stdcall dev_GetRenderTarget(IDirect3DDevice8 *s, IDirect3DSurface8 **pp)
{ (void)s; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_GetDepthStencilSurface(IDirect3DDevice8 *s, IDirect3DSurface8 **pp)
{ (void)s; if (pp) *pp = NULL; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_SetViewport(IDirect3DDevice8 *s, const D3DVIEWPORT8 *vp)
{ (void)s; (void)vp; return D3D_OK; }
static HRESULT __stdcall dev_GetViewport(IDirect3DDevice8 *s, D3DVIEWPORT8 *vp)
{
    (void)s;
    if (!vp) return D3DERR_INVALIDCALL;
    memset(vp, 0, sizeof *vp);
    vp->Width = vk.lw; vp->Height = vk.lh; vp->MaxZ = 1.0f;
    return D3D_OK;
}
static HRESULT __stdcall dev_SetMaterial(IDirect3DDevice8 *s, const D3DMATERIAL8 *m) { (void)s; (void)m; return D3D_OK; }
static HRESULT __stdcall dev_GetMaterial(IDirect3DDevice8 *s, D3DMATERIAL8 *m) { (void)s; (void)m; return D3D_OK; }
static HRESULT __stdcall dev_SetLight(IDirect3DDevice8 *s, DWORD i, const D3DLIGHT8 *l) { (void)s; (void)i; (void)l; return D3D_OK; }
static HRESULT __stdcall dev_GetLight(IDirect3DDevice8 *s, DWORD i, D3DLIGHT8 *l) { (void)s; (void)i; (void)l; return D3D_OK; }
static HRESULT __stdcall dev_LightEnable(IDirect3DDevice8 *s, DWORD i, BOOL en) { (void)s; (void)i; (void)en; return D3D_OK; }
static HRESULT __stdcall dev_SetVertexShader(IDirect3DDevice8 *s, DWORD h) { (void)s; (void)h; return D3D_OK; }
static HRESULT __stdcall dev_GetVertexShader(IDirect3DDevice8 *s, DWORD *p) { (void)s; if (p) *p = 0; return D3D_OK; }
static HRESULT __stdcall dev_SetVertexShaderConstant(IDirect3DDevice8 *s, INT reg, const void *d, DWORD n)
{ (void)s; (void)reg; (void)d; (void)n; return D3D_OK; }
static HRESULT __stdcall dev_SetPixelShader(IDirect3DDevice8 *s, DWORD h) { (void)s; (void)h; return D3D_OK; }
static HRESULT __stdcall dev_GetPixelShader(IDirect3DDevice8 *s, DWORD *p) { (void)s; if (p) *p = 0; return D3D_OK; }
static HRESULT __stdcall dev_SetPixelShaderConstant(IDirect3DDevice8 *s, INT reg, const void *d, DWORD n)
{ (void)s; (void)reg; (void)d; (void)n; return D3D_OK; }
static void __stdcall dev_SetGammaRamp(IDirect3DDevice8 *s, DWORD f, const D3DGAMMARAMP *r) { (void)s; (void)f; (void)r; }
static void __stdcall dev_GetGammaRamp(IDirect3DDevice8 *s, D3DGAMMARAMP *r) { (void)s; (void)r; }
static HRESULT __stdcall dev_SetPalette(IDirect3DDevice8 *s, DWORD n, const void *e) { (void)s; (void)n; (void)e; return D3D_OK; }
static HRESULT __stdcall dev_BeginPush(IDirect3DDevice8 *s, DWORD c, DWORD **pp) { (void)s; (void)c; (void)pp; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_EndPush(IDirect3DDevice8 *s, DWORD *p) { (void)s; (void)p; return D3DERR_INVALIDCALL; }
static HRESULT __stdcall dev_Swap(IDirect3DDevice8 *s, DWORD flags)
{ (void)flags; return dev_Present(s, NULL, NULL, NULL, NULL); }

static const IDirect3DDevice8Vtbl g_device_vtbl = {
    dev_QueryInterface, dev_AddRef, dev_Release,
    dev_GetDirect3D, dev_GetDeviceCaps, dev_GetDisplayMode, dev_GetCreationParameters,
    dev_Reset, dev_Present, dev_GetBackBuffer,
    dev_BeginScene, dev_EndScene, dev_Clear,
    dev_SetTransform, dev_GetTransform,
    dev_SetRenderState, dev_GetRenderState,
    dev_SetTextureStageState, dev_GetTextureStageState,
    dev_SetTexture, dev_GetTexture,
    dev_SetStreamSource, dev_GetStreamSource,
    dev_SetIndices, dev_GetIndices,
    dev_DrawPrimitive, dev_DrawIndexedPrimitive,
    dev_DrawPrimitiveUP, dev_DrawIndexedPrimitiveUP,
    dev_CreateTexture, dev_CreateVertexBuffer, dev_CreateIndexBuffer,
    dev_CreateRenderTarget, dev_CreateDepthStencilSurface,
    dev_SetRenderTarget, dev_GetRenderTarget, dev_GetDepthStencilSurface,
    dev_SetViewport, dev_GetViewport,
    dev_SetMaterial, dev_GetMaterial,
    dev_SetLight, dev_GetLight, dev_LightEnable,
    dev_SetVertexShader, dev_GetVertexShader, dev_SetVertexShaderConstant,
    dev_SetPixelShader, dev_GetPixelShader, dev_SetPixelShaderConstant,
    dev_SetGammaRamp, dev_GetGammaRamp,
    dev_SetPalette,
    dev_BeginPush, dev_EndPush,
    dev_Swap,
    NULL, /* appended CreateImageSurface */
    dev_CreateCubeTexture,
    NULL, /* appended CreateVolumeTexture */
};

/* ── Bring-up ──────────────────────────────────────────────────────────── */

static int has_ext(const VkExtensionProperties *e, uint32_t n, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (!strcmp(e[i].extensionName, name))
            return 1;
    return 0;
}

static void swapchain_make(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkSurfaceFormatKHR fmts[64];
    VkPresentModeKHR modes[16];
    uint32_t nf = 64, nm = 16, i;
    VkSwapchainKHR old = vk.swap;
    int w = 0, h = 0;

    if (!vk.surface)
        return;
    if (scene_wsi_defer())
        return;
    VkResult waited = vkDeviceWaitIdle(vk.dev);
    VKC(waited);
    if (!scene_wsi_wait_ok(waited))
        return;
    overlay_release();
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys, vk.surface, &caps) != VK_SUCCESS)
        return;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys, vk.surface, &nf, fmts);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys, vk.surface, &nm, modes);

    ci.surface = vk.surface;
    ci.imageFormat = fmts[0].format;
    ci.imageColorSpace = fmts[0].colorSpace;
    for (i = 0; i < nf; i++) {
        /* A UNORM format: the picture is already display-referred. */
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            ci.imageFormat = fmts[i].format;
            ci.imageColorSpace = fmts[i].colorSpace;
            break;
        }
    }
    overlay_format(ci.imageFormat);
    ci.imageExtent = caps.currentExtent;
    if (caps.currentExtent.width == 0xFFFFFFFFu) {
        if (s_have_host && s_host.drawable_size)
            s_host.drawable_size(&w, &h, s_host.user);
        ci.imageExtent.width = (uint32_t)(w > 0 ? w : 1280);
        ci.imageExtent.height = (uint32_t)(h > 0 ? h : 960);
    }
    if (!ci.imageExtent.width || !ci.imageExtent.height)
        return;                             /* minimised: nothing to show */
    ci.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount)
        ci.minImageCount = caps.maxImageCount;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (getenv("RECOMP_WINDOW_SHOT") && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    /* The title's own timer paces frames, so the display should not as well
     * unless asked: waiting on both halves the frame rate when they beat.
     * Asked, it still only does on a display refreshing at a multiple of 60
     * -- the Direct3D 11 backend's rule (d3d8_present_interval_for). The
     * Steam Deck OLED's panel runs at 90 Hz and FIFO there held the title to
     * about 40 frames a second. An unknown rate (0) does not sync either, as
     * on Windows: SDL's X11 driver under Xwayland reports none. */
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    {
        unsigned hz = (s_have_host && s_host.refresh_hz) ? s_host.refresh_hz(s_host.user) : 0;
        unsigned k = (hz + 30) / 60, nearest = k * 60;
        unsigned off = hz > nearest ? hz - nearest : nearest - hz;
        int sync = s_vsync && k >= 1 && k <= 4 && off <= 1;
        static unsigned told;
        if (s_vsync && !sync && told++ < 4)
            LOG("display refreshes at %u Hz, not a multiple of 60: vsync not used", hz);
        if (!sync)
            for (i = 0; i < nm; i++)
                if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR || modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    ci.presentMode = modes[i];
                    break;
                }
    }
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    if (vkCreateSwapchainKHR(vk.dev, &ci, NULL, &vk.swap) != VK_SUCCESS) {
        vk.swap = VK_NULL_HANDLE;
        LOG("could not create a swap chain; drawing without presenting");
    } else {
        vk.swap_n = 8;
        vkGetSwapchainImagesKHR(vk.dev, vk.swap, &vk.swap_n, vk.swap_img);
        vk.swap_ext = ci.imageExtent;
        vk.swap_fmt = ci.imageFormat;
        vk.swap_usage = ci.imageUsage;
        LOG("swap chain %ux%u, %u images, present mode %d", ci.imageExtent.width,
            ci.imageExtent.height, vk.swap_n, (int)ci.presentMode);
    }
    if (old)
        vkDestroySwapchainKHR(vk.dev, old, NULL);
    scene_wsi_changed();
}

static int device_make(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkExtensionProperties iext[256], dext[512];
    VkPhysicalDevice devs[8];
    VkQueueFamilyProperties qf[16];
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures available, enabled={0};
    const char *inst_ext[32], *dev_ext[8], *layers[1];
    uint32_t n_iext = 256, n_dext = 512, n_inst = 0, n_dev = 0, n_devs = 8, n_qf = 16, i;
    float prio = 1.0f;
    int want_surface = s_have_host && s_host.create_surface && !getenv("RECOMP_HEADLESS");

    vkEnumerateInstanceExtensionProperties(NULL, &n_iext, iext);
    if (want_surface)
        for (i = 0; i < s_host.instance_extension_count && n_inst < 28; i++)
            inst_ext[n_inst++] = s_host.instance_extensions[i];
    /* MoltenVK is a "portability" driver, which the loader hides unless asked. */
    if (has_ext(iext, n_iext, "VK_KHR_portability_enumeration")) {
        inst_ext[n_inst++] = "VK_KHR_portability_enumeration";
        ici.flags |= 0x00000001;            /* ENUMERATE_PORTABILITY */
    }
    if (has_ext(iext, n_iext, "VK_KHR_get_physical_device_properties2"))
        inst_ext[n_inst++] = "VK_KHR_get_physical_device_properties2";
    app.pApplicationName = g_window_title;
    app.apiVersion = VK_API_VERSION_1_1;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = n_inst;
    ici.ppEnabledExtensionNames = inst_ext;
    if (getenv("RECOMP_VK_VALIDATE")) {
        layers[0] = "VK_LAYER_KHRONOS_validation";
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = layers;
    }
    if (vkCreateInstance(&ici, NULL, &vk.inst) != VK_SUCCESS) {
        ici.enabledLayerCount = 0;
        if (vkCreateInstance(&ici, NULL, &vk.inst) != VK_SUCCESS) {
            LOG("no Vulkan instance (is a Vulkan driver installed?)");
            return 0;
        }
    }
    if (vkEnumeratePhysicalDevices(vk.inst, &n_devs, devs) < 0 || !n_devs) {
        LOG("no Vulkan device");
        return 0;
    }
    vk.phys = devs[0];
    for (i = 0; i < n_devs; i++) {
        vkGetPhysicalDeviceProperties(devs[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            vk.phys = devs[i];
            break;
        }
    }
    vkGetPhysicalDeviceProperties(vk.phys, &props);
    if(!raster_limits_from_properties(&props.limits,&vk.max_raster_width,&vk.max_raster_height)) {
        LOG("device has no compatible full-attachment viewport extent");return 0;
    }
    vkGetPhysicalDeviceFeatures(vk.phys,&available);
    const char *af=getenv("RECOMP_MAX_ANISOTROPY");
    vk.max_anisotropy=1.0f;
    if(af && !strcmp(af,"1") && available.samplerAnisotropy
       && isfinite(props.limits.maxSamplerAnisotropy) && props.limits.maxSamplerAnisotropy>1.0f) {
        enabled.samplerAnisotropy=VK_TRUE;
        vk.max_anisotropy=props.limits.maxSamplerAnisotropy;
    }
    dci.pEnabledFeatures=&enabled;
    vkGetPhysicalDeviceMemoryProperties(vk.phys, &vk.memprops);
    vk.ubo_align = props.limits.minUniformBufferOffsetAlignment;
    if (vk.ubo_align < 16)
        vk.ubo_align = 16;

    if (want_surface) {
        if (!s_host.create_surface(vk.inst, &vk.surface, s_host.user)) {
            LOG("the host made no surface; drawing without presenting");
            vk.surface = VK_NULL_HANDLE;
        }
    }

    vkGetPhysicalDeviceQueueFamilyProperties(vk.phys, &n_qf, qf);
    vk.qfam = 0xFFFFFFFFu;
    for (i = 0; i < n_qf; i++) {
        VkBool32 ok = VK_TRUE;
        if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;
        if (vk.surface)
            vkGetPhysicalDeviceSurfaceSupportKHR(vk.phys, i, vk.surface, &ok);
        if (ok) {
            vk.qfam = i;
            break;
        }
    }
    if (vk.qfam == 0xFFFFFFFFu) {
        LOG("no queue family that draws%s", vk.surface ? " and presents" : "");
        return 0;
    }

    vkEnumerateDeviceExtensionProperties(vk.phys, NULL, &n_dext, dext);
    if (vk.surface)
        dev_ext[n_dev++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (has_ext(dext, n_dext, "VK_KHR_portability_subset"))
        dev_ext[n_dev++] = "VK_KHR_portability_subset";
    qci.queueFamilyIndex = vk.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = n_dev;
    dci.ppEnabledExtensionNames = dev_ext;
    if (vkCreateDevice(vk.phys, &dci, NULL, &vk.dev) != VK_SUCCESS) {
        LOG("no Vulkan logical device");
        return 0;
    }
    vkGetDeviceQueue(vk.dev, vk.qfam, 0, &vk.queue);
    if(af && !strcmp(af,"1"))LOG("max anisotropy %.1fx for eligible 2D RGB mip chains",vk.max_anisotropy);
    LOG("%s, Vulkan %u.%u.%u%s", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion),
        vk.surface ? "" : " (no window: off screen)");
    return 1;
}

static int resources_make(void)
{
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkAttachmentDescription att[2];
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    VkDescriptorSetLayoutBinding b[3 + 2 * STAGES];
    VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize ps[3];
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkFormatProperties fp;
    VkCommandBuffer cbs[2];
    VkImageView views[2];
    VkImageAspectFlags daspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    int i;

    /* 24-bit depth with stencil is what the console has; Apple GPUs offer
     * only the 32-bit float one. */
    vk.depth_fmt = VK_FORMAT_D24_UNORM_S8_UINT;
    vkGetPhysicalDeviceFormatProperties(vk.phys, vk.depth_fmt, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        vk.depth_fmt = VK_FORMAT_D32_SFLOAT_S8_UINT;

    pci.queueFamilyIndex = vk.qfam;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(vk.dev, &pci, NULL, &vk.pool) != VK_SUCCESS)
        return 0;
    cai.commandPool = vk.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 2;
    if (vkAllocateCommandBuffers(vk.dev, &cai, cbs) != VK_SUCCESS)
        return 0;
    vk.cmd = cbs[0];
    vk.cmd_up = cbs[1];
    if (vkCreateFence(vk.dev, &fci, NULL, &vk.fence) != VK_SUCCESS)
        return 0;
    {
        VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(vk.dev, &sci, NULL, &vk.acquired) != VK_SUCCESS)
            return 0;
    }

    if (!image_make(vk.tw, vk.th, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &vk.color, &vk.color_mem, &vk.color_view)
            || !image_make(vk.tw, vk.th, vk.depth_fmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           daspect, &vk.depth, &vk.depth_mem, &vk.depth_view)
            || !image_make(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, &vk.dummy, &vk.dummy_mem, &vk.dummy_view))
        return 0;
    if(!image_make_layers(1,1,1,6,1,VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,
          &vk.dummy_cube,&vk.dummy_cube_mem,&vk.dummy_cube_view))return 0;

    memset(att, 0, sizeof att);
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[1].format = vk.depth_fmt;
    for (i = 0; i < 2; i++) {
        att[i].samples = VK_SAMPLE_COUNT_1_BIT;
        att[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    att[0].initialLayout = att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[1].initialLayout = att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    memset(&sub, 0, sizeof sub);
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;
    rpci.attachmentCount = 2;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    if (vkCreateRenderPass(vk.dev, &rpci, NULL, &vk.pass) != VK_SUCCESS)
        return 0;
    views[0] = vk.color_view;
    views[1] = vk.depth_view;
    fbci.renderPass = vk.pass;
    fbci.attachmentCount = 2;
    fbci.pAttachments = views;
    fbci.width = vk.tw;
    fbci.height = vk.th;
    fbci.layers = 1;
    if (vkCreateFramebuffer(vk.dev, &fbci, NULL, &vk.fb) != VK_SUCCESS)
        return 0;

    /* Bindings as the HLSL registers map: b0-b2 at 0-2, t0-t3 at 8-11,
     * s0-s3 at 16-19 (the bases given to shaderc below). */
    memset(b, 0, sizeof b);
    for (i = 0; i < 3; i++) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        b[i].descriptorCount = 1;
        b[i].stageFlags = i == 0 ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    }
    for (i = 0; i < STAGES; i++) {
        b[3 + i].binding = 8 + (uint32_t)i;
        b[3 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        b[3 + i].descriptorCount = 1;
        b[3 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[3 + STAGES + i].binding = 16 + (uint32_t)i;
        b[3 + STAGES + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        b[3 + STAGES + i].descriptorCount = 1;
        b[3 + STAGES + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    dlci.bindingCount = 3 + 2 * STAGES;
    dlci.pBindings = b;
    if (vkCreateDescriptorSetLayout(vk.dev, &dlci, NULL, &vk.dsl) != VK_SUCCESS)
        return 0;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &vk.dsl;
    if (vkCreatePipelineLayout(vk.dev, &plci, NULL, &vk.pl) != VK_SUCCESS)
        return 0;
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; ps[0].descriptorCount = 3 * 8192;
    ps[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;          ps[1].descriptorCount = STAGES * 8192;
    ps[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;                ps[2].descriptorCount = STAGES * 8192;
    dpci.maxSets = 8192;
    dpci.poolSizeCount = 3;
    dpci.pPoolSizes = ps;
    if (vkCreateDescriptorPool(vk.dev, &dpci, NULL, &vk.dpool) != VK_SUCCESS)
        return 0;

    if (!ring_make(&vk.vtx, 48u << 20, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            || !ring_make(&vk.idx, 4u << 20, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            || !ring_make(&vk.ubo, 16u << 20, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            || !ring_make(&vk.stage, 64u << 20, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            || !ring_make_ex(&vk.readback, (VkDeviceSize)vk.tw * vk.th * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1))
        return 0;

    /* The attachments into the layouts the render pass expects, and the
     * stand-in for an empty stage cleared to nothing, as Direct3D samples it. */
    {
        VkClearColorValue zero;
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        memset(&zero, 0, sizeof zero);
        VKC(vkBeginCommandBuffer(vk.cmd, &bi));
        image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(vk.cmd, vk.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        sn_touch(vk.color);
        image_layout(vk.cmd, vk.color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        image_layout(vk.cmd, vk.depth, daspect, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        image_layout(vk.cmd, vk.dummy, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(vk.cmd, vk.dummy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        sn_touch(vk.dummy);
        image_layout(vk.cmd, vk.dummy, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        /* TextureCube requires a cube view even when the stage is empty. */
        range.layerCount=6;
        image_layout_layers(vk.cmd,vk.dummy_cube,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,1,0,6);
        vkCmdClearColorImage(vk.cmd,vk.dummy_cube,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&zero,1,&range);
        sn_touch(vk.dummy_cube);
        image_layout_layers(vk.cmd,vk.dummy_cube,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,1,0,6);
        VKC(vkEndCommandBuffer(vk.cmd));
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        VKC(vkQueueSubmit(vk.queue, 1, &si, vk.fence));
        VKC(vkWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX));
    }

    vk.sc = shaderc_compiler_initialize();
    vk.sc_opts = shaderc_compile_options_initialize();
    if (!vk.sc || !vk.sc_opts)
        return 0;
    shaderc_compile_options_set_source_language(vk.sc_opts, shaderc_source_language_hlsl);
    shaderc_compile_options_set_hlsl_io_mapping(vk.sc_opts, true);
    shaderc_compile_options_set_hlsl_offsets(vk.sc_opts, true);
    shaderc_compile_options_set_auto_map_locations(vk.sc_opts, true);
    shaderc_compile_options_set_binding_base(vk.sc_opts, shaderc_uniform_kind_buffer, 0);
    shaderc_compile_options_set_binding_base(vk.sc_opts, shaderc_uniform_kind_texture, 8);
    shaderc_compile_options_set_binding_base(vk.sc_opts, shaderc_uniform_kind_sampler, 16);
    shaderc_compile_options_set_optimization_level(vk.sc_opts, shaderc_optimization_level_performance);
    cache_open();
    vk.vs_fixed = shader_make(s_vs_fixed, 1, "vs_fixed");
    vk.ps_tex = shader_make(s_ps_tex, 0, "ps_tex");
    vk.ps_notex = shader_make(s_ps_notex, 0, "ps_notex");
    return vk.vs_fixed && vk.ps_tex && vk.ps_notex;
}

static HRESULT __stdcall d3d_QueryInterface(IDirect3D8 *s, const IID *iid, void **pp)
{ (void)s; (void)iid; (void)pp; return D3DERR_INVALIDCALL; }
static ULONG __stdcall d3d_AddRef(IDirect3D8 *s)  { (void)s; return 1; }
static ULONG __stdcall d3d_Release(IDirect3D8 *s) { (void)s; return 0; }

static HRESULT __stdcall d3d_CreateDevice(IDirect3D8 *s, UINT adapter, DWORD devtype,
        HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    (void)s; (void)adapter; (void)devtype; (void)hwnd; (void)flags;
    if (!out || !pp)
        return D3DERR_INVALIDCALL;
    if (g_device_made) {
        *out = &g_device;
        return D3D_OK;
    }
    memset(&vk, 0, sizeof vk);
    vk.lw = pp->BackBufferWidth ? pp->BackBufferWidth : 640;
    vk.lh = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
    vk.scale = s_scaling ? s_render_scale : 1;
    vk.tw = s_scaling ? d3d8_canvas_width(vk.lw, vk.lh, vk.scale, s_canvas_num, s_canvas_den)
                      : vk.lw * vk.scale;
    vk.th = vk.lh * vk.scale;
    if (!device_make()
        || (s_guest_raster_explicit && !raster_size(vk.lw,vk.lh,1,&vk.tw,&vk.th))
        || !resources_make()) {
        LOG("the device could not be created (requested raster/device limits checked)");
        return D3DERR_INVALIDCALL;
    }
    vk.rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    vk.rs[D3DRS_ZWRITEENABLE] = 1;
    vk.rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    vk.rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    vk.rs[D3DRS_BLENDOP] = 1;
    vk.rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    vk.rs[D3DRS_COLORWRITEENABLE] = 0xF;
    vk.ready = 1;
    sn_live=sn_enabled;
    sx_device_ready();
    {
        static int registered;
        if (!registered++)
            atexit(cache_save_at_exit);
    }
    if (vk.surface)
        swapchain_make();
    if(s_guest_raster_explicit)LOG("guest RGBA raster density %u/%u; logical aliases preserved; procedural RGB565/depth-only unchanged",s_guest_raster_num,s_guest_raster_den);
    LOG("ready: %ux%u target (%ux%u at scale %u), depth format %s", vk.tw, vk.th, vk.lw, vk.lh,
        vk.scale, vk.depth_fmt == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32FS8");
    g_device.lpVtbl = &g_device_vtbl;
    g_device_made = 1;
    *out = &g_device;
    return D3D_OK;
}

static const IDirect3D8Vtbl g_d3d8_vtbl = {
    d3d_QueryInterface, d3d_AddRef, d3d_Release,
    d3d_CreateDevice,
};
static IDirect3D8 g_d3d8 = { &g_d3d8_vtbl };

/* ── Public ────────────────────────────────────────────────────────────── */

IDirect3D8 *xbox_Direct3DCreate8(UINT sdk) { (void)sdk; return &g_d3d8; }
IDirect3DDevice8 *xbox_GetD3DDevice(void) { return g_device_made ? &g_device : NULL; }
IDirect3DDevice8 *d3d8_GetDevice(void) { return xbox_GetD3DDevice(); }
void d3d8_PresentFrame(void) { if (g_device_made) dev_Present(&g_device, NULL, NULL, NULL, NULL); }
unsigned d3d8_GetRenderScale(void) { return vk.scale ? vk.scale : 1; }
/* Host pixels per title pixel, across and down (a widescreen canvas is wider). */
float d3d8_GetCanvasScaleX(void) { return vk.lw ? (float)vk.tw / (float)vk.lw : (float)d3d8_GetRenderScale(); }
float d3d8_GetCanvasScaleY(void) { return vk.lh ? (float)vk.th / (float)vk.lh : (float)d3d8_GetRenderScale(); }
UINT d3d8_GetBackbufferWidth(void) { return vk.lw; }
UINT d3d8_GetBackbufferHeight(void) { return vk.lh; }

/* Exact current bindings on the GPU executor after ordered backend submission. */
int d3d8_vk_snapshot_configure(D3D8VkSnapshotOwner owner,void*user){
 if(vk.ready||sn_enabled||!owner)return 0;
 sn_owner=owner;sn_owner_user=user;sn_enabled=1;return 1;
}
static int sn_lock(void*unused){(void)unused;return sn_accessible();}
static void sn_unlock(void*unused){(void)unused;}
static int sn_generation_get(void*unused,VfpGeneration*g,uint64_t*device){
 (void)unused;if(!sn_accessible()||!vk.ready)return 0;
 *g=sn_generation;*device=sn_device_generation;return g->scene&&g->view;
}
static int sn_resolve(void*unused,const VfpResource*resource,VksSource*out){
 (void)unused;if(!sn_accessible()||!resource||!out)return 0;
 return vkr_find(&sn_versions,resource->identity,resource->version,out);
}
static int sn_binding(VkImage image,VkImageLayout layout,unsigned role,uint32_t fmt,uint32_t w,uint32_t h,uint32_t pitch,VfpResource*out){
 VksSource*entry=vkr_image(&sn_versions,image);
 if(!entry||!entry->content_version)return 0;
 entry->layout=layout;
 *out=(VfpResource){entry->storage_identity,entry->content_version,role,fmt,w,h,pitch};return 1;
}
int d3d8_vk_snapshot_bindings(VfpResource*out,uint32_t capacity,uint32_t*count){
 VfpResource resources[6];unsigned n=0;
 if(!out||!count||capacity<6||!vk.ready||!sn_accessible()||(rt_active&&rt_active->depth_only))return 0;
 /* No unsubmitted upload/draw may follow the image transition/copy. Flush
  * first, still on the same executor; future draws rebind through frame_begin. */
 frame_submit();
 VkImage color=rt_active?rt_active->tex.img:vk.color;
 uint32_t w=rt_active?rt_active->tex.width:vk.lw,h=rt_active?rt_active->tex.height:vk.lh;
 if(!sn_binding(color,rt_active?rt_active->layout:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VFP_COLOR,rt_active?rt_active->tex.format:D3DFMT_LIN_A8R8G8B8,w,h,rt_active?rt_active->desc.color_pitch:w*4,&resources[n++]))return 0;
 VkImage depth=rt_active?(rt_active->depth?rt_active->depth->img:VK_NULL_HANDLE):vk.depth;
 if(depth&&!sn_binding(depth,rt_active?rt_active->depth->layout:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VFP_DEPTH,rt_active&&rt_active->depth->zformat==1?D3DFMT_LIN_D16:D3DFMT_LIN_D24S8,w,h,rt_active?rt_active->desc.zeta_pitch:w*4,&resources[n++]))return 0;
 const NV2ACombinerState*state=d3d8_combiners_nv2a_state();
 for(unsigned stage=0;stage<4;stage++){
  VkTex*t=vk.tex[stage];if(t&&sx_view(t,stage)!=t->view)return 0; /* filteredUI snapshot not implemented: never export original bytes as filtered view */
  VkImage image=t?t->img:VK_NULL_HANDLE;VkImageLayout layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if(t&&!t->borrowed){
   if(!t->levels||t->levels>MAX_TEX_LEVELS)return 0;
   uint32_t mask=(1u<<t->levels)-1;
   if(t->cube){for(unsigned face=0;face<6;face++)if((t->face_have[face]&mask)!=mask)return 0;}
   else if((t->have&mask)!=mask)return 0;
  }
  if(!image){unsigned mode=state?(state->texture_program>>(stage*5))&31u:0;image=mode==3? vk.dummy_cube:vk.dummy;}
  /* Both normalized/linear RT views and depth sample views share this exact
   * registry image, storage identity and content version. */
  for(unsigned i=0;i<rt_color_n;i++)if(rt_colors[i]->tex.img==image)layout=rt_colors[i]->layout;
  for(unsigned i=0;i<rt_depth_n;i++)if(rt_depths[i]->img==image)layout=rt_depths[i]->layout;
  if(!sn_binding(image,layout,VFP_TEXTURE0+stage,t?t->format:0,t?t->width:1,t?t->height:1,t?t->pitch:4,&resources[n++]))return 0;
 }
 memcpy(out,resources,n*sizeof*resources);*count=n;return 1;
}
int d3d8_vk_snapshot_capture(VfpBudget*budget,const VfpSource*draw,VfpPacket**out){
 if(!out)return VFP_INVALID;
 *out=NULL;
 if(!draw||!budget||!draw->generation.scene||!draw->generation.view||!sn_accessible())return VFP_NO_PROVIDER;
 VfpResource resources[6];uint32_t count=0;
 if(!d3d8_vk_snapshot_bindings(resources,6,&count))return VFP_RESOURCE;
 sn_generation=draw->generation;
 if(!sn_context){VksAccess access={NULL,sn_lock,sn_unlock,sn_resolve,sn_generation_get};sn_context=vks_create(vk.phys,vk.dev,vk.queue,vk.qfam,&access,64u*1024u*1024u);if(!sn_context)return VFP_RESOURCE;}
 VfpSource source=*draw;source.resource_count=count;source.resources=resources;source.texture_mask=15;
 VfpProvider provider=vks_provider(sn_context);return vfp_capture(budget,&provider,&source,out);
}
VksContext*d3d8_vk_snapshot_context(void){return sn_accessible()?sn_context:NULL;}
int d3d8_vk_snapshot_release(VfpPacket**packet){if(!sn_accessible())return 0;vfp_release(packet);if(sn_context)vks_collect(sn_context);return 1;}
int d3d8_vk_snapshot_close(void){return sn_accessible()&&vks_destroy(&sn_context);}

/* Monotonic actual scene draw issue count, not PGRAPH success/clear/present. */
uint64_t d3d8_vk_snapshot_draw_serial(void){return sn_accessible()?sn_draw_serial:0;}
int d3d8_vk_snapshot_draw_state(void*out,uint32_t cap,uint32_t*bytes){
 struct SnapshotDrawState {
  uint32_t version,bytes;DWORD rs[MAX_RS],tss[STAGES][32];
  uint32_t scissor[4];float blend[4];int polygon_offset;float slope,units;
  int points;float point_size;float frame_constants[12];NV2APSConstants ps_constants;NV2ACombinerState combiner;
 } state;
 if(!out||!bytes||cap<sizeof state||!sn_accessible()||!vk.ready||!vk.frame_open||!vk.ubo.map||
    (uint64_t)vk.dyn_off[2]+sizeof state.frame_constants>vk.ubo.size||
    (uint64_t)vk.dyn_off[0]+sizeof state.ps_constants>vk.ubo.size)return 0;
 const NV2ACombinerState*c=d3d8_combiners_nv2a_state();if(!c)return 0;
 memset(&state,0,sizeof state);state.version=1;state.bytes=sizeof state;
 memcpy(state.rs,vk.rs,sizeof state.rs);memcpy(state.tss,vk.tss,sizeof state.tss);
 memcpy(state.scissor,vk.scissor,sizeof state.scissor);memcpy(state.blend,vk.blend_color,sizeof state.blend);
 state.polygon_offset=vk.polygon_offset;state.slope=vk.polygon_slope;state.units=vk.polygon_units;
 state.points=vk.points;state.point_size=vk.point_size;state.combiner=*c;
 memcpy(state.frame_constants,vk.ubo.map+vk.dyn_off[2],sizeof state.frame_constants);
 memcpy(&state.ps_constants,vk.ubo.map+vk.dyn_off[0],sizeof state.ps_constants);
 memcpy(out,&state,sizeof state);*bytes=sizeof state;return 1;
}

/* Included in proposed backend after actual registry/RT/readback definitions. */
static int coherence_shape(const RjpQuery*q,uint32_t bytes){
 if(q&&q->format==18u){
  RjpMip m;unsigned type=(q->header[0]>>16)&7u;
  if(q->level||!q->resource||((q->header[3]>>8)&255u)!=18u)return 0;
  if(type==4u){if(q->parent||!rjp_mip_at(q->header,0,&m))return 0;}
  else if(type==5u){
   if(q->parent){if(!rjp_mip_surface(q->parent,q->parent_header,q->header,&m))return 0;}
   else{uint32_t p[5];memcpy(p,q->header,sizeof p);p[0]=(p[0]&~0x70000u)|0x40000u;if(!rjp_mip_at(p,0,&m))return 0;}
  }else return 0;
  return q->resolved_data==m.bits&&q->width==m.width&&q->height==m.height&&q->pitch==m.pitch&&bytes==m.bytes;
 }
 if(!q||(((q->header[0]>>16)&7u)!=4u&&((q->header[0]>>16)&7u)!=5u)||q->format!=6||!q->resource||!q->header[1]||((q->header[3]>>8)&255u)!=6u||!q->width||!q->height||q->width>512||q->height>512||
 (q->width&(q->width-1))||(q->height&(q->height-1))||q->pitch!=q->width*4u||bytes!=(uint64_t)q->pitch*q->height)return 0;
 if(((q->header[0]>>16)&7u)==4u){
  /* Parent identity remains authentic and unchanged. A nonzero mip Data is
   * an exact original-factory view, not a forged parent texture header. */
  RjpMip mip;if(q->parent||!rjp_mip_at(q->header,q->level,&mip))return 0;
  return q->resolved_data==mip.bits&&q->width==mip.width&&q->height==mip.height&&q->pitch==mip.pitch;
 }
 /* Legacy direct Surface reads retain their original level0 shape contract.
  * The Save exporter uses its still-live parent for all new mip readbacks. */
 if(q->level)return 0;
 return q->resolved_data==q->header[1]||(q->header[1]<0x04000000u&&q->resolved_data==0x80000000u+q->header[1]);
}
static int cd_enabled;
static _Thread_local D3D8VkCoherenceDiagnostic*cd_scope;
void d3d8_vk_coherence_diagnostic_scope(D3D8VkCoherenceDiagnostic*d){cd_scope=cd_enabled?d:NULL;}
void d3d8_vk_coherence_diagnostic_reject(uint32_t stage,const RjpQuery*q){
 if(cd_scope&&!cd_scope->stage){cd_scope->version=1;cd_scope->stage=stage;if(q)cd_scope->query=*q;}
}
void d3d8_vk_coherence_diagnostic_result(int result){if(cd_scope){cd_scope->backend_result=result;cd_scope->observed|=16;}}
static int cd_test(int ok,uint32_t stage,const RjpQuery*q){
 if(!ok){d3d8_vk_coherence_diagnostic_reject(stage,q);}return ok;
}
#define CD_REQUIRE(expr,stage,ret,q) do{if(!cd_test(!!(expr),stage,q))return ret;}while(0)
int d3d8_vk_coherence_read(const RjpQuery*input,void*out,uint32_t bytes,D3D8VkCoherenceProof*proof){
 CD_REQUIRE(input&&out&&proof,MCD_READ_ARGS,0,input);
 CD_REQUIRE(sn_accessible(),MCD_READ_OWNER,0,input);
 CD_REQUIRE(rjp_enabled(),MCD_READ_TRACKING,0,input);
 CD_REQUIRE(vk.ready,MCD_READ_DEVICE,0,input);
 RjpQuery q=*input;
 CD_REQUIRE(coherence_shape(&q,bytes),MCD_READ_SHAPE,0,&q);
 XboxNv2aDrainState before,after;uint32_t base,size;uint64_t epoch;
 CD_REQUIRE(xbox_Nv2aDrainSnapshot(&before),MCD_DRAIN_BEFORE,0,&q);
 if(cd_scope){cd_scope->get=before.get;cd_scope->put=before.put;cd_scope->observed|=2;}
 CD_REQUIRE(xbox_ContiguousEpoch(q.resolved_data,bytes,&base,&size,&epoch),MCD_EPOCH_BEFORE,0,&q);
 if(cd_scope){cd_scope->base=base;cd_scope->size=size;cd_scope->epoch=epoch;cd_scope->observed|=4;}
 RtColor*c=rt_find(q.resolved_data);
 CD_REQUIRE(c,MCD_RT_MISSING,0,&q);
 CD_REQUIRE(!c->depth_only,MCD_RT_DEPTH,0,&q);
 CD_REQUIRE(c->desc.width==q.width,MCD_RT_WIDTH,0,&q);
 CD_REQUIRE(c->desc.height==q.height,MCD_RT_HEIGHT,0,&q);
 CD_REQUIRE(c->desc.color_pitch==q.pitch,MCD_RT_PITCH,0,&q);
 CD_REQUIRE(!(c->desc.format&0xf000u),MCD_RT_FORMAT_HIGH,0,&q);
 CD_REQUIRE((c->desc.format&255u)==0x28u,MCD_RT_FORMAT_LOW,0,&q);
 frame_submit();
 VksSource*image=vkr_image(&sn_versions,c->tex.img);
 CD_REQUIRE(image,MCD_REGISTRY_MISSING,0,&q);
 CD_REQUIRE(image->content_version,MCD_REGISTRY_CONTENT,0,&q);
 q.allocation=image->storage_identity;q.contents=image->content_version;q.guest_epoch=epoch;
 RjpProof source;int attested=rjp_query(&q,&source);
 if(!attested&&cd_scope)rjp_query_diagnostic(&q,&cd_scope->rjp);
 CD_REQUIRE(attested,MCD_RJP_QUERY,0,&q);
 if(cd_scope){cd_scope->source=source;cd_scope->observed|=8;}
 /* GPU images contain active pixels only. Owned row padding is deterministic;
  * the typed frontend must copy active rows and preserve original guest padding. */
 unsigned char*staging=calloc(1,bytes);CD_REQUIRE(staging,MCD_STAGING,0,&q);
 int ok=d3d8_vk_rt_readback(q.resolved_data,staging,q.pitch);
 if(!ok)d3d8_vk_coherence_diagnostic_reject(MCD_READBACK,&q);
 uint64_t after_epoch=0;uint32_t after_base=0,after_size=0;
 image=vkr_image(&sn_versions,c->tex.img);RjpProof validated;
 ok=ok&&cd_test(rt_find(q.resolved_data)==c,MCD_POST_RT,&q)&&
 cd_test(image!=NULL,MCD_POST_REGISTRY,&q)&&cd_test(image->storage_identity==q.allocation,MCD_POST_ALLOCATION,&q)&&
 cd_test(image->content_version==q.contents,MCD_POST_CONTENT,&q)&&
 cd_test(xbox_ContiguousEpoch(q.resolved_data,bytes,&after_base,&after_size,&after_epoch),MCD_POST_EPOCH_LOOKUP,&q)&&
 cd_test(after_epoch==epoch,MCD_POST_EPOCH,&q)&&cd_test(after_base==base,MCD_POST_BASE,&q)&&cd_test(after_size==size,MCD_POST_SIZE,&q)&&
 cd_test(xbox_Nv2aDrainSnapshot(&after),MCD_POST_DRAIN,&q)&&cd_test(after.get==before.get,MCD_POST_GET,&q)&&
 cd_test(after.put==before.put,MCD_POST_PUT,&q)&&cd_test(rjp_query(&q,&validated),MCD_POST_RJP,&q)&&
 cd_test(validated.guest_id==source.guest_id,MCD_POST_GUEST,&q)&&cd_test(validated.producer_serial==source.producer_serial,MCD_POST_SERIAL,&q);
 /* after scalars were initialized above; do not inspect uninitialized after
  * drain or validated proof when the original short circuit skipped them. */
 if(cd_scope){cd_scope->after_epoch=after_epoch;cd_scope->after_base=after_base;cd_scope->after_size=after_size;}
 if(ok){D3D8VkCoherenceProof result={source,after.get,after.put,q.width,q.height,q.pitch};memcpy(out,staging,bytes);*proof=result;}
 free(staging);return ok;
}

int d3d8_vk_coherence_source(const RjpQuery*q,void*out,uint32_t bytes,D3D8VkCoherenceProof*proof){
 CD_REQUIRE(q&&out&&proof,MCD_SOURCE_ARGS,-1,q);
 CD_REQUIRE(sn_accessible(),MCD_SOURCE_OWNER,-1,q);
 CD_REQUIRE(rjp_enabled(),MCD_SOURCE_TRACKING,-1,q);
 CD_REQUIRE(vk.ready,MCD_SOURCE_DEVICE,-1,q);
 CD_REQUIRE(coherence_shape(q,bytes),MCD_SOURCE_SHAPE,-1,q);
 uint32_t base,size;uint64_t epoch=0;
 if(q->resolved_data>=0x80000000u)CD_REQUIRE(xbox_ContiguousEpoch(q->resolved_data,bytes,&base,&size,&epoch),MCD_SOURCE_EPOCH,-1,q);
 int classification=rjp_cpu_classify(q->resolved_data,bytes,epoch);
 if(cd_scope){cd_scope->classify=classification;cd_scope->bytes=bytes;cd_scope->observed|=1;}
 if(classification==0)return 0;
 return d3d8_vk_coherence_read(q,out,bytes,proof)?1:-1;
}
#undef CD_REQUIRE

/* Shared initializer: only identical actual executor authority is reusable.
 * General configure remains one-shot; no late/foreign owner can join it. */
static int snapshot_executor_owner(void*unused){(void)unused;return nv2a_executor_is_owner();}
int d3d8_vk_snapshot_executor_configure(void){
 if(vk.ready||vk.dev||g_device_made)return 0;
 if(sn_enabled)return sn_owner==snapshot_executor_owner&&sn_owner_user==NULL;
 return d3d8_vk_snapshot_configure(snapshot_executor_owner,NULL);
}
int d3d8_vk_coherence_configure(void){
 if(vk.ready||vk.dev||g_device_made)return 0;
 if(!d3d8_vk_snapshot_executor_configure()||!rjp_configure(nv2a_executor_is_owner))return 0;
 int covered=rjp_coverage_begin();
 const char*diag=getenv("TOPSPIN_CREATOR_MATERIAL_DIAGNOSTICS");
 cd_enabled=covered&&diag&&!strcmp(diag,"1");return covered;
}

/* Include at end of exporter94 backend candidate TU. Own work djultra64.
 * Requires original SDK93 executor ownership and exporter94 allocation ledger.
 * Filtering is prepared before SetTexture, NEVER from inside bind_draw/pass. */
#include "sprite_xbr_vk.h"
#include "sprite_xbr_source.h"
#include "d3d8_sprite_xbr.h"
#include <stdatomic.h>
static _Atomic unsigned sx_capable;
static void sx_device_ready(void){atomic_store_explicit(&sx_capable,sxvk_supported(vk.phys,vk.qfam),memory_order_release);}
static unsigned sx_factor=1;
static SxVkContext*sx_context;
static _Atomic int sx_init_failed;
typedef struct SxFrozen {VksContext*context;VfpProvider provider;VfpResource resource;void*lease;struct SxFrozen*next;} SxFrozen;
static SxFrozen*sx_pending;
static int sx_owner(void*u){(void)u;return vk.ready&&sn_accessible();}
static int sx_epoch(void*u,VfpGeneration*g,uint64_t*device){(void)u;if(!sx_owner(NULL))return 0;*device=sn_device_generation;*g=(VfpGeneration){sn_device_generation,sn_device_generation};return 1;}
/* Filter lifecycle epoch above is NOT a fabricated title scene/camera. No
 * packet/camera/simulation semantics are imported into this resource cache. */
static int sx_resolve(void*u,const VfpResource*r,VksSource*out){(void)u;return sx_owner(NULL)&&vkr_find(&sn_versions,r->identity,r->version,out);}
static void sx_collect_sources(void){
 SxFrozen**p=&sx_pending;while(*p){SxFrozen*f=*p;vks_collect(f->context);if(vks_destroy(&f->context)){*p=f->next;free(f);}else p=&f->next;}
}
static uint64_t sx_pending_bytes(void){uint64_t n=0;for(SxFrozen*f=sx_pending;f;f=f->next)n+=vks_charged_bytes(f->context);return n;}
static uint64_t sx_external(void*u){(void)u;return sx_pending_bytes();}
static unsigned sx_pending_count(void){unsigned n=0;for(SxFrozen*f=sx_pending;f;f=f->next)n++;return n;}
static void sx_retire_source(SxFrozen*f){
 if(f->lease){f->provider.release(f->provider.user,&f->resource,f->lease);f->lease=NULL;}
 vks_collect(f->context);if(vks_destroy(&f->context))free(f);else{f->next=sx_pending;sx_pending=f;}
}
static void sx_release(void*u,SxVkSnapshot*s){(void)u;SxFrozen*f=s->lease;sx_retire_source(f);memset(s,0,sizeof*s);}
static int sx_acquire(void*u,const SxVkKey*k,uint64_t allowance,SxVkSnapshot*out){
 (void)u;if(!sx_owner(NULL))return 0;frame_submit();sx_collect_sources();if(sx_pending_count()>=32||!allowance)return 0;
 VksSource exact;if(!vkr_find(&sn_versions,k->allocation,k->version,&exact)||exact.device_generation!=k->device_generation||exact.width!=k->width||exact.height!=k->height||exact.mips!=1||exact.layers!=1||exact.format!=VK_FORMAT_R8G8B8A8_UNORM||exact.aspects!=VK_IMAGE_ASPECT_COLOR_BIT||exact.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return 0;
 SxFrozen*f=calloc(1,sizeof*f);if(!f)return 0;VksAccess a={NULL,sn_lock,sn_unlock,sx_resolve,sx_epoch};f->context=vks_create(vk.phys,vk.dev,vk.queue,vk.qfam,&a,allowance);if(!f->context){free(f);return 0;}
 f->resource=(VfpResource){k->allocation,k->version,VFP_TEXTURE0,k->guest_format,k->width,k->height,k->width*4};f->provider=vks_provider(f->context);
 if(!f->provider.retain(f->provider.user,&f->resource,&f->lease)){sx_retire_source(f);return 0;}
 VkImage image;VkFormat format;uint32_t w,h;if(!vks_image(f->context,f->lease,&image,&format,&w,&h)){sx_retire_source(f);return 0;}
 *out=(SxVkSnapshot){image,format,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,w,h,vks_charged_bytes(f->context),f};return 1;
}
static int sx_init(void){
 if(sx_context)return 1;if(sx_init_failed||!sx_owner(NULL)||!d3d8_vk_sprite_filter_supported())return 0;sx_init_failed=1;
 shaderc_compile_options_t options=shaderc_compile_options_initialize();if(!options)return 0;shaderc_compile_options_set_source_language(options,shaderc_source_language_glsl);shaderc_compile_options_set_target_env(options,shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_0);
 shaderc_compile_options_add_macro_definition(options,"FINAL_RGBA8",11,"0",1);shaderc_compilation_result_t a=shaderc_compile_into_spv(vk.sc,sxvk_shader_source,sizeof sxvk_shader_source-1,shaderc_compute_shader,"sprite_xbr.comp","main",options);
 shaderc_compile_options_release(options);options=shaderc_compile_options_initialize();if(!options){if(a)shaderc_result_release(a);return 0;}shaderc_compile_options_set_source_language(options,shaderc_source_language_glsl);shaderc_compile_options_set_target_env(options,shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_0);shaderc_compile_options_add_macro_definition(options,"FINAL_RGBA8",11,"1",1);
 shaderc_compilation_result_t b=shaderc_compile_into_spv(vk.sc,sxvk_shader_source,sizeof sxvk_shader_source-1,shaderc_compute_shader,"sprite_xbr.comp","main",options);shaderc_compile_options_release(options);
 if(a&&b&&shaderc_result_get_compilation_status(a)==shaderc_compilation_status_success&&shaderc_result_get_compilation_status(b)==shaderc_compilation_status_success){SxVkAccess access={NULL,sx_owner,sx_acquire,sx_release,sx_external};sx_context=sxvk_create(vk.phys,vk.dev,vk.queue,vk.qfam,sn_device_generation,&access,(const uint32_t*)shaderc_result_get_bytes(a),shaderc_result_get_length(a),(const uint32_t*)shaderc_result_get_bytes(b),shaderc_result_get_length(b));}
 if(a)shaderc_result_release(a);if(b)shaderc_result_release(b);if(sx_context)sx_init_failed=0;return sx_context!=NULL;
}
int d3d8_vk_set_sprite_filter(unsigned factor){if(vk.ready||(factor!=1&&factor!=2&&factor!=4))return 0;sx_factor=factor;return 1;}
int d3d8_vk_sprite_filter_supported(void){return atomic_load_explicit(&sx_capable,memory_order_acquire)!=0;}
unsigned d3d8_vk_sprite_filter_requested(void){return sx_factor;}
unsigned d3d8_vk_sprite_filter_current(void){return sn_enabled&&!atomic_load_explicit(&sx_init_failed,memory_order_acquire)&&d3d8_vk_sprite_filter_supported()?sx_factor:1;}
void d3d8_vk_sprite_texture_admit(void*texture,uint32_t profile,unsigned uploaded_now){
 VkTex*t=texture;if(!t)return;if(!profile)t->sprite_auth=(SxVkAuth){0};t->sprite_view=VK_NULL_HANDLE;t->sprite_storage=t->sprite_version=0;
 if(sx_factor==1||!profile||profile>3||!sx_owner(NULL)||t->cube||t->borrowed||t->format!=D3DFMT_DXT3||t->levels!=1||!(t->have&1)||!t->img)return;
 /* Cache hits do not flush. On a miss, sx_acquire submits earlier original
  * commands before provider94 copies, still before next draw ring allocation. */
 VksSource*current=vkr_image(&sn_versions,t->img);if(!current||!current->content_version)return;
 if(!sxvk_auth(&t->sprite_auth,profile,(int)uploaded_now,current->storage_identity,current->content_version))return;
 current->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;SxVkKey key={current->storage_identity,current->content_version,current->device_generation,profile,t->width,t->height,0x0e,sx_factor};
 if(!sx_init())return;VkImageView result=sxvk_get(sx_context,&key);if(result){t->sprite_view=result;t->sprite_storage=key.allocation;t->sprite_version=key.version;}
}
/* Called from bind_draw WITHOUT submit, allocation, owner mutation or filtering.
 * A direct later Upload/LockRect bumps ledger version; stale filtered content
 * cannot be used even if the next draw bypasses the generic ready bind. */
static VkImageView sx_view(const VkTex*t,unsigned stage){
 if(!t||!t->sprite_view||!sx_owner(NULL))return t?t->view:VK_NULL_HANDLE;
 const NV2ACombinerState*cs=d3d8_combiners_nv2a_state();if(!cs||!sxvk_draw_policy(stage,cs->texture_program,cs->explicit_nv2a,cs->texture_control[0],vk.rs[D3DRS_ALPHATESTENABLE]))return t->view;
 VksSource*s=vkr_image(&sn_versions,t->img);return s&&s->storage_identity==t->sprite_storage&&s->content_version==t->sprite_version?t->sprite_view:t->view;
}
int d3d8_vk_sprite_filter_close_idle(void){if(!sx_owner(NULL))return 0;if(!sxvk_destroy_idle(&sx_context))return 0;sx_collect_sources();return sx_pending==NULL;}

#include "visual_scene_provider.inc"

#include "visual_scene_host_policy.inc"

#include "visual_scene_gpu_draw.inc"

#include "visual_scene_replay_device.inc"

#include "visual_scene_wsi.inc"

#include "visual_scene_source_session.inc"
