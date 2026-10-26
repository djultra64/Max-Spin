#include "nv2a_executor_job.h"
#include "resource_provenance.h"
#include "resource_coherence_backend.h"
#include "resource_epoch_api.h"
#ifdef XBOXRECOMP_VISUAL_EXPORT
#include "pb_visual_packet_backend.h"
#include "visual_scene_journal_backend.h"
#endif
#include "../d3d/d3d8_fog.h"
/**
 * Execute the parts of the title's pushbuffer that produce visible pixels.
 *
 * The title builds NV2A commands in guest RAM and advances DMA_PUT; without
 * something consuming them the framebuffer stays whatever it was, which is how
 * a fully booted title renders a black screen. This walks the same command
 * stream nv2a_pb_scan.c surveys and carries out the subset that decides what is
 * on screen: which surface is being drawn into, and clearing it.
 *
 * It also rasterises geometry, but only the part that can be drawn honestly:
 * batches whose attribute 0 is already in screen space, flat-shaded, straight
 * into the same guest framebuffer the clear writes. Titles draw their UI, HUD
 * and 2D overlays that way, so it is the first geometry to appear. Batches that
 * need a vertex program executed are counted and skipped rather than drawn
 * somewhere wrong -- see raster_batch(). Texturing, depth and vertex programs
 * are still a renderer, not a command decoder; the upgrade path is the D3D11
 * translator in src/nv2a/nv2a_pgraph_d3d11.c.
 *
 * Everything this does not handle is counted and ranked by
 * nv2a_pb_exec_report(), so what remains is a list rather than a guess.
 *
 * Enabled with RECOMP_PB_EXEC. RECOMP_RASTER_TEST draws one known triangle
 * after every clear, which separates "the pixel path is broken" from "the title
 * has not given us any vertices". RECOMP_FB_DUMP=<prefix> writes the surface to
 * <prefix>NNN.bmp, so the result can be looked at without a display.
 */
#include <math.h>
#include "visual_pb_hooks.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nv2a_flip.h"
#include "kernel.h"   /* XBOX_CONTIG_BASE / XBOX_CONTIG_SIZE */
#include "xbox_memory_layout.h"   /* xbox_Nv2aFrameCounterFlip */
/* The swizzle decoder the D3D8 layer already uses -- one implementation of
 * Morton order, not a second one that can disagree with it. */
#include "../d3d/d3d8_swizzle.h"
#include "nv2a_vsh_interp.h"
#include "nv2a_combiner.h"
#include "nv2a_pb_point_capture.h"
#include "nv2a_pb_hair_capture.h"
#if defined(_WIN32)
#include <windows.h>
#define NV_TLS __declspec(thread)
#else
#define NV_TLS __thread
#endif
#include "../nv2a/nv2a_pgraph_d3d11.h"   /* the D3D11 translator this feeds */
#include "nv2a_vsh_cpu.h"                 /* shared vertex-program HLSL adapter */

extern ptrdiff_t xbox_GetMemoryOffset(void);
extern void xbox_FramebufferWindowSet(uint32_t fb_va, uint32_t pitch);
extern void xbox_FramebufferWindowStart(void);
extern uint32_t g_xbox_image_lo, g_xbox_image_hi;

/* Periodic progress markers are diagnostics, not execution controls.
 * Exact opt-in only; errors and explicit capture/trace paths are unchanged.
 * Own integration: djultra64. */
static int runtime_quiet_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("RECOMP_RUNTIME_QUIET");
        enabled = value && !strcmp(value, "1");
    }
    return enabled;
}

/* Would writing this surface land on the title's own image?
 *
 * NV097_SET_SURFACE_COLOR_OFFSET is an offset within the colour DMA object,
 * not a guest virtual address, and this executor has always used it as one.
 * That is harmless while the two happen to agree and catastrophic when they do
 * not: the Xbox Dashboard names surface 0x00088000 at 1280x960x4, so clearing
 * it wrote 4.9 MB of opaque black from 0x00088000 to 0x00538000 -- straight
 * over its own code, its D3D context at 0x000BBFC0 and the register-block
 * pointer at 0x000BE2C4. The symptom was a title that submitted one perfect
 * frame and then spun forever in a pushbuffer-full loop, three layers away,
 * with every D3D global reading 0xFF000000: the clear colour.
 *
 * So refuse, and say so. Getting the address right needs the DMA object base
 * this ignores (NV097_SET_CONTEXT_DMA_COLOR); until that exists, writing
 * nothing is strictly better than writing over the guest, and a title that
 * cannot draw is easier to debug than one that has been overwritten.
 */
static int surface_hits_image(uint32_t base, uint32_t bytes)
{
    if (!g_xbox_image_hi || !bytes)
        return 0;
    return base < g_xbox_image_hi && base + bytes > g_xbox_image_lo;
}

/* Where a DMA-object offset actually lives.
 *
 * NV097_SET_SURFACE_COLOR_OFFSET is an offset inside the colour DMA object,
 * and for a framebuffer that object covers physical memory -- so the offset
 * is a physical address, not a guest VA. Those are the same number in this
 * runtime, which is why treating it as a VA works until it does not: on
 * hardware the image is mapped at VA 0x00010000 from arbitrary physical
 * pages, so a framebuffer at physical 0x84000 does not overlap it. Here it
 * would.
 *
 * The title tells us which it is by where it allocated. Half-Life 2's
 * framebuffer comes from MmAllocateContiguousMemory, which this runtime
 * serves from the window at XBOX_CONTIG_BASE, so physical P is visible at
 * XBOX_CONTIG_BASE + P -- clear of the image, and the same bytes the title's
 * own writes and the framebuffer window reach.
 *
 * So: use the offset as a VA when that is credible, and fall back to the
 * physical mirror exactly when it is not. Titles whose surfaces already sit
 * in ordinary RAM (Wreckless renders to the tiled alias of physical
 * 0x01954000) keep the first path and are unaffected.
 */
static uint32_t dma_resolve(uint32_t offset)
{
    extern uint32_t xbox_ContiguousAllocatedBytes(void);
    extern uint32_t xbox_PhysicalToVirtual(uint32_t pa);
    extern int xbox_PhysicalNamespaceEnabled(void);

    /* Private disjoint namespace: exact low-RAM backing conversion, including
     * DMA spans beyond the first translated page. Never use high-water guessing. */
    if (offset >= 0x04000000u && offset < 0x08000000u &&
            xbox_PhysicalNamespaceEnabled())
        return xbox_PhysicalToVirtual(offset);


    /* Did this runtime hand the offset out as contiguous memory? Then the
     * bytes live in the window, and that is not a guess: the arena is a bump
     * allocator from XBOX_CONTIG_BASE, so everything below its high-water
     * mark is memory some MmAllocateContiguousMemory call returned. The
     * title's own writes go through the window, so the executor's must too.
     *
     * Checking this BEFORE the image test is the whole point. The image test
     * only catches an offset that would land on the title's code, and whether
     * it does is an accident of where the image happens to end: Half-Life 2's
     * colour surface is physical 0x00A6C000, which clears the image by 700 KB.
     * So it looked like an ordinary VA, and the executor cleared 1.2 MB of
     * black straight through the guest heap -- which faulted the title three
     * frames later on a pointer that had been overwritten, while the real
     * framebuffer at 0x80A6C000 stayed untouched and the screen stayed black. */
    if (offset < xbox_ContiguousAllocatedBytes())
        return XBOX_CONTIG_BASE + offset;
    if (!surface_hits_image(offset, 1))
        return offset;
    if ((uint64_t)offset < XBOX_CONTIG_SIZE)
        return XBOX_CONTIG_BASE + offset;
    return offset;                         /* nothing better to offer */
}

static int surface_write_refused(uint32_t base, uint32_t bytes, const char *what)
{
    static int said;

    if (!surface_hits_image(base, bytes))
        return 0;
    if (!said) {
        said = 1;
        fprintf(stderr,
                "  [GPU] REFUSING to %s surface 0x%08X..0x%08X: that overlaps "
                "the loaded image (0x%08X..0x%08X).\n"
                "  [GPU]   SET_SURFACE_COLOR_OFFSET is a DMA-object offset, not "
                "a guest VA, and this executor treats it as one. Writing here "
                "would destroy the title's own code and globals.\n",
                what, base, base + bytes, g_xbox_image_lo, g_xbox_image_hi);
        fflush(stderr);
    }
    return 1;
}

/* NV097 methods this executor acts on. */
/* Blending. The pair this title programs, read from its own pushbuffer
 * rather than guessed: BLEND_ENABLE written 1168 times and left on,
 * SFACTOR 0x0302 (SRC_ALPHA) and DFACTOR 0x0303 (ONE_MINUS_SRC_ALPHA).
 * ALPHA_TEST_ENABLE is written 390 times and left at zero, so this is
 * blending and not an alpha test. */
#define NV097_SET_BLEND_ENABLE            0x0304
#define NV097_SET_BLEND_FUNC_SFACTOR      0x0344
#define NV097_SET_BLEND_FUNC_DFACTOR      0x0348
#define NV_BLEND_SRC_ALPHA                0x0302
#define NV_BLEND_ONE_MINUS_SRC_ALPHA      0x0303
#define NV097_SET_BLEND_COLOR             0x034C
#define NV097_SET_BLEND_EQUATION          0x0350

#define NV097_SET_SURFACE_CLIP_HORIZONTAL 0x0200
#define NV097_SET_SURFACE_CLIP_VERTICAL   0x0204
#define NV097_SET_SURFACE_FORMAT          0x0208
#define NV097_SET_SURFACE_PITCH           0x020C
#define NV097_SET_SURFACE_COLOR_OFFSET    0x0210
#define NV097_SET_COLOR_CLEAR_VALUE       0x1D90
#define NV097_CLEAR_SURFACE               0x1D94
#define NV097_SET_VERTEX_DATA_ARRAY_OFFSET 0x1720   /* +i*4, 16 attributes */
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT 0x1760   /* +i*4 */
#define NV097_SET_BEGIN_END               0x17FC
#define NV097_SET_TEXTURE_OFFSET          0x1B00   /* +i*0x40 */
#define NV097_SET_TEXTURE_FORMAT          0x1B04
/* 0x1B20, as in nv2a_regs.h. This copy said 0x1B0C, which is CONTROL0, so the
 * texture's enable/LOD word was taken as its palette address and every
 * palettised texture was expanded through whatever memory that pointed at:
 * Def Jam's text, drawn into I8 textures, came out as solid blue boxes with
 * glyphs missing. */
#define NV097_SET_TEXTURE_PALETTE         0x1B20   /* +i*0x40 */
#define NV097_TEXFMT_SZ_I8_A8R8G8B8       0x0B     /* 8-bit index, ARGB palette */
#define NV097_TEXFMT_LU_IMAGE_A8R8G8B8    0x12     /* linear ARGB, what I8 expands to */
#define NV097_SET_TEXTURE_ADDRESS         0x1B08
#define NV097_SET_TEXTURE_CONTROL0        0x1B0C   /* bit 30: stage enable */
#define NV097_SET_TEXTURE_CONTROL1        0x1B10
#define NV097_SET_TEXTURE_IMAGE_RECT      0x1B1C
/* The buffer flip. A title double-buffers by telling the GPU which buffer
 * the CRTC reads and which it draws into, advancing the write index and
 * then stalling until the flip has happened. Ignoring these means the
 * stall never clears: Half-Life 2's loader submits its initialisation,
 * asks for a flip, and waits for it in a loop that makes no kernel calls
 * and burns no dispatch, which reads as a hang with no cause.
 *
 * ponytail: the flip completes the moment it is asked for, because there is
 * no scanout to be in the middle of. That makes every frame land instantly
 * and a title that paces itself on the flip runs as fast as it can draw.
 * Pacing wants the vblank clock in the kernel, not a sleep in here. */
#define NV097_SET_FLIP_READ               0x0120
#define NV097_SET_FLIP_WRITE              0x0124
#define NV097_SET_FLIP_MODULO             0x0128
#define NV097_FLIP_INCREMENT_WRITE        0x012C
#define NV097_FLIP_STALL                  0x0130
#define NV097_ARRAY_ELEMENT16             0x1800
/* Draw a run of vertices straight out of the arrays, with no index list:
 * bits 0..23 are the first vertex, bits 24..31 the count minus one. It may
 * appear several times inside one BEGIN_END to draw a longer run. */
#define NV097_ARRAY_ELEMENT32             0x1808
#define NV097_DRAW_ARRAYS                 0x1810
#define NV097_INLINE_ARRAY                0x1818
/* Immediate-mode vertices. SET_VERTEX3F/4F carry the position, and writing
 * its last component completes a vertex using whatever the SET_VERTEX_DATA*
 * registers currently hold for the other attributes. This is how Half-Life
 * 2's Xbox loader and the game's own 2D drawing submit every quad -- neither
 * uses INLINE_ARRAY -- so without these the executor saw SET_BEGIN_END pairs
 * with nothing attached and reported `draws 0` while a million and a half
 * textured quads a minute went past it. */
#define NV097_SET_VERTEX3F                0x1500   /* +0..0x08, 3 floats */
#define NV097_SET_VERTEX4F                0x1518   /* +0..0x0C, 4 floats */
#define NV097_SET_VERTEX_DATA2F_M         0x1880   /* + attr*8,  2 floats */
#define NV097_SET_VERTEX_DATA4F_M         0x1A00   /* + attr*16, 4 floats */
#define NV097_SET_VERTEX_DATA4UB          0x1940   /* + attr*4,  4 x u8 */
#define NV097_SET_VERTEX_DATA2S           0x1900   /* + attr*4,  2 x s16 */
#define NV097_SET_VERTEX_DATA4S_M         0x1980   /* + attr*8,  4 x s16 */

/* One immediate vertex, as this file packs it for the shared draw path:
 * position float4, diffuse D3DCOLOR, texcoord0 float2. */
/* An immediate-mode vertex: all 16 attributes as float4. */
#define IMM_VERTEX_DWORDS (NV_VERTEX_ATTRS * 4)

#define NV097_CLEAR_COLOR_MASK            0xF0   /* R,G,B,A bits */

/* One vertex attribute stream, as the title describes it. Attribute 0 is
 * position; the rest are colours, texture coordinates and so on. */
typedef struct {
    uint32_t offset;      /* guest address of element 0 */
    uint32_t type;        /* NV097 data type nibble */
    uint32_t size;        /* components per element */
    uint32_t stride;      /* bytes between elements */
} VertexAttr;

#define NV_VERTEX_ATTRS 16
#define NV_MAX_INDICES  32768
#define NV_MAX_INLINE   65536           /* dwords of INLINE_ARRAY per batch */

/* Texture stage 0, decoded from what the title programmed.
 *
 * Only stage 0: it is the only one the dashboard configures, and a stage
 * nothing writes to is a stage nothing can be sampled from. The rest arrive
 * as unhandled methods and are counted as such, which is how the next title
 * that needs them will say so. */
typedef struct {
    uint32_t offset;                    /* guest address of texel (0,0)  */
    uint32_t width, height;             /* from IMAGE_RECT               */
    uint32_t pitch;                     /* bytes per row, from CONTROL1  */
    uint32_t color;                     /* NV097 colour-format code      */
    uint32_t addr_u, addr_v;            /* wrap mode per axis            */
    uint32_t palette;                   /* guest address of the CLUT, P8 */
    uint32_t levels;                    /* mip levels, from FORMAT       */
    uint32_t filter;                    /* SET_TEXTURE_FILTER            */
    int      cube;                      /* FORMAT: six faces, not one    */
    uint32_t palette_len;               /* 256, 128, 64 or 32 entries */
    int disabled;                       /* CONTROL0 enable bit clear */
    int      valid;
} Texture;

static struct {
    VertexAttr attr[NV_VERTEX_ATTRS];
    uint32_t   prim;                    /* SET_BEGIN_END parameter, 0 = ended */
    uint32_t   idx[NV_MAX_INDICES];
    uint32_t   idx_count;
    /* INLINE_ARRAY payload: vertices written straight into the pushbuffer
     * instead of into a buffer the title points at. Same vertex format, a
     * different place to read them from. */
    uint32_t   inline_buf[NV_MAX_INLINE];
    uint32_t   inline_count;
    /* Current values of the immediate-mode attributes, and how many complete
     * vertices they have produced in this batch. */
    /* Immediate mode: every attribute's current value, as the SET_VERTEX
     * methods leave it. Writing attribute 0 (position) emits a vertex. */
    float      imm_attr[NV_VERTEX_ATTRS][4];
    uint16_t   imm_used;                /* attributes written since BEGIN */
    uint32_t   imm_count;
    int        inline_active;
    uint32_t   draws, verts, nonzero_draws;
    float      min_x, max_x, min_y, max_y;
    uint32_t color_offset, color_base, pitch, format;
    /* The surface the last batch actually drew into. A double-buffered title
     * has already pointed color_offset at the next buffer and cleared it by
     * the time the flip arrives, so dumping the current one dumps the frame
     * that has not been drawn yet -- which is how a correctly rendered
     * sequence came out as 12 black BMPs. */
    uint32_t drawn_offset;
    /* ...and the shape of that surface. A title also draws into small
     * render targets (Burnout 3: 128x128 shadow/reflection maps, pitch 512),
     * often after the main scene, so "last surface drawn" alone picked one of
     * those and read it with the framebuffer's pitch: horizontal noise. The
     * biggest surface drawn since the last flip is the one being presented. */
    uint32_t drawn_pitch, drawn_x, drawn_y, drawn_w, drawn_h, drawn_bpp;
    int drawn_stale;      /* set at a flip: the next draw starts a new frame */
    uint64_t pixels;
    uint32_t pixel_max;   /* brightest value any pixel write carried */
    uint32_t clip_x, clip_w, clip_y, clip_h;
    uint32_t clear_color;
    uint32_t clear_rect_h,clear_rect_v,clear_rect_known;
    uint32_t clears, unhandled_total;
    uint32_t flip_read, flip_write, flip_modulo, flips;
    uint32_t tris_drawn, tris_skipped_offscreen, batches_untransformed;
    /* Why a batch came out flat. "Untextured" has two causes that look
     * identical on screen and want opposite fixes: the batch carried no
     * texture coordinates, or it did and the stage was not usable. */
    uint32_t batches_textured, batches_no_uv, batches_no_tex;
    uint32_t blend_enable, blend_sfactor, blend_dfactor;
    uint32_t blend_equation, blend_color;
    uint32_t color_mask;        /* SET_COLOR_MASK: A<<24 R<<16 G<<8 B */
    /* Visibility tests (D3D's Begin/EndVisibilityTest): pixels that pass the
     * depth test while counting is on, reported by GET_REPORT. */
    uint32_t zpass_enable, zpass_count, reports;
    uint32_t blend_pairs[16];   /* sfactor<<16 | dfactor seen, for the report */
    int blend_npairs;
    Texture  texs[4];                   /* one per texture stage */
    uint32_t texture_control[4], texture_key[4];
    unsigned texture_key_known[4];
    /* Register combiners and the per-pixel state around them. rc_seen: the
     * title has programmed the combiners, so they decide every pixel's
     * colour; until then the old "stage 0 texel * diffuse" stands in. */
    Nv2aCombiner rc;
    int      rc_seen;
    uint32_t clip_plane_mode;           /* SET_SHADER_CLIP_PLANE_MODE */
    uint32_t alpha_test, alpha_func, alpha_ref;
    uint32_t fog_enable, fog_mode, fog_color, fog_gen;
    float fog_plane[4],fog_modelview[16];uint16_t fog_modelview_written;
    float    fog_param[2];
    /* Vertex programs (nv2a_vsh_interp.c) and the depth buffer, which
     * together are what a 3D scene needs and a 2D one never used. */
    uint32_t xf_mode;                   /* TRANSFORM_EXECUTION_MODE: 2 = program */
    float fixed_composite[16];          /* row-major, includes title viewport */
    uint16_t fixed_composite_written;
    uint32_t batches_program, verts_program, tris_behind;
    /* Where program-path triangles go, so "nothing drew" has a reason. */
    uint32_t xf_degenerate, xf_offscreen, xf_drawn;
    uint64_t xf_depth_fail, xf_pixels;
    float xf_min[3], xf_max[3];
    uint32_t xf_fmt[16];  /* attribute formats seen: type | size<<4 | slot<<8 */
    int xf_nfmt, xf_seeded;
    uint32_t depth_test, depth_func, depth_mask;
    uint32_t zeta_offset, zstencil_clear, zeta_pitch;
} s_gpu;

/* Current attributes and transform mode have one owner. */
int nv2a_vsh_active(void)
{
    return (s_gpu.xf_mode & 3u) == 2 && nv2a_vsh_program_loaded();
}
void nv2a_vsh_current(int attr, float out[4])
{
    if (attr >= 0 && attr < NV_VERTEX_ATTRS)
        memcpy(out, s_gpu.imm_attr[attr], sizeof s_gpu.imm_attr[attr]);
    else
        memset(out, 0, 4 * sizeof *out);
}

/* Unhandled methods, ranked. The interesting output is not that something was
 * skipped but which things dominate, because that is the order to implement
 * them in. */
#define PB_EXEC_MAX_UNHANDLED 2048
typedef struct { uint32_t method, count, last_param; } PbUnhandled;
static PbUnhandled s_unhandled[PB_EXEC_MAX_UNHANDLED];
static int s_unhandled_count;

/* Every texture-stage register, as the title last set it.
 * Texturing is not implemented yet; knowing which formats and sizes a title
 * actually programs is what decides which ones are worth implementing. */
#define NV_TEX_FIRST 0x1B00
#define NV_TEX_LAST  0x1BFC
static uint32_t s_tex_reg[(NV_TEX_LAST - NV_TEX_FIRST) / 4 + 1];
static uint8_t  s_tex_set[(NV_TEX_LAST - NV_TEX_FIRST) / 4 + 1];

/* Formats whose dimensions come from the format word and whose coordinates
 * arrive normalised, rather than from a pitch and SET_TEXTURE_IMAGE_RECT with
 * coordinates in texels. Swizzled and block-compressed are both in this group,
 * and every place that used to test only for swizzled needs the pair. */
static int tex_size_from_format(uint32_t fmt)
{
    return d3d8_format_is_swizzled(fmt) || d3d8_format_dxt_block_bytes(fmt);
}

static void tex_update_valid(Texture *t)
{
    t->valid = t->offset && t->width && t->height
            && (tex_size_from_format(t->color) || t->pitch) && !t->disabled;
}

/* d3d8_resources.c; its parameter is the D3DFORMAT enum, which this file does
 * not see and which is passed as a plain integer. */
unsigned int d3d8_format_bpp(uint32_t fmt);

/* Texels one row of a linear texture spans in memory: the pitch, which can be
 * wider than the image. The translator is sent the whole row width, so rows
 * land where they are, and coordinates are normalised by it. (Def Jam's movie
 * frames once came in 1344-byte rows for 640 texels; that was a lifter bug
 * padding every aligned row by 64 bytes (patch 0066), and they are 1280 now,
 * but a title may still choose a wider pitch.) */
static uint32_t linear_row_texels(const Texture *t)
{
    uint32_t bpp;

    if (tex_size_from_format(t->color) || !t->pitch)
        return t->width;
    bpp = d3d8_format_bpp(t->color) / 8u;
    if (!bpp || t->pitch % bpp || t->pitch / bpp < t->width)
        return t->width;
    return t->pitch / bpp;
}

static void record_tex_reg(uint32_t method, uint32_t param)
{
    s_tex_reg[(method - NV_TEX_FIRST) / 4] = param;
    s_tex_set[(method - NV_TEX_FIRST) / 4] = 1;
    /* A pitch is a linear texture's property. A swizzled one has no rows and
     * so no pitch, and requiring one here refused every swizzled texture --
     * which is nearly all of them, since swizzled is the Xbox default. That
     * left the title's own textures unsampled and every textured quad drawn in
     * flat vertex colour. */
}

/* Every distinct texture a batch was drawn with, and how many batches used it.
 *
 * The per-draw verbose print shows the first few draws of the first frame,
 * which is enough to see that texturing works at all and not enough to answer
 * "is a font page ever bound". This is the same shape as the unhandled-method
 * table below it: a small set, ranked, printed with the rest of the report. */
#define PB_EXEC_MAX_TEXTURES 64
typedef struct {
    uint32_t offset, color, width, height, batches;
} PbTexUse;
static PbTexUse s_tex_use[PB_EXEC_MAX_TEXTURES];
static int s_tex_use_count;

/* Defined below, next to the sampler it goes through. */
static void dump_texture_bmp(uint32_t seq);

/* Repeat dumps are numbered from well past the first-use sequence, so a
 * listing sorts them after the textures they came from and no first-use file
 * is ever overwritten by one. */
#define TEX_DUMP_SEQ_BASE 1000u
#define TEX_DUMP_SEQ_MAX  40u

static void note_texture_use(void)
{
    int i;

    if (!s_gpu.texs[0].valid)
        return;
    for (i = 0; i < s_tex_use_count; i++) {
        if (s_tex_use[i].offset == s_gpu.texs[0].offset
         && s_tex_use[i].color  == s_gpu.texs[0].color) {
            s_tex_use[i].batches++;
            /* Dump a surface that is redrawn, every Nth time it is bound.
             *
             * First use alone cannot tell a decode error that is wrong in
             * every frame from one that accumulates across them. A block
             * transform that is wrong is wrong on its own, in the keyframe
             * as much as anywhere; motion compensation that is wrong starts
             * from a clean keyframe and smears further with each predicted
             * frame after it. In a single frame the two look identical, and
             * in a sequence they look nothing alike -- so the sequence is
             * what has to be captured.
             *
             * It belongs on this side of the return: a video surface keeps
             * one address for the whole film, so after the first frame it is
             * only ever found here, and the first-use dump below never fires
             * for it again. RECOMP_TEX_DUMP_EVERY=<n> sets the interval, and
             * RECOMP_TEX_DUMP still names the files. */
            {
                static int every = -1;
                static unsigned binds, seq;
                if (every < 0) {
                    const char *e = getenv("RECOMP_TEX_DUMP_EVERY");
                    every = e ? atoi(e) : 0;
                }
                if (every > 0 && ++binds % (unsigned)every == 0
                    && seq < TEX_DUMP_SEQ_MAX)
                    dump_texture_bmp(TEX_DUMP_SEQ_BASE + seq++);
            }
            return;
        }
    }
    if (s_tex_use_count < PB_EXEC_MAX_TEXTURES) {
        s_tex_use[s_tex_use_count].offset  = s_gpu.texs[0].offset;
        s_tex_use[s_tex_use_count].color   = s_gpu.texs[0].color;
        s_tex_use[s_tex_use_count].width   = s_gpu.texs[0].width;
        s_tex_use[s_tex_use_count].height  = s_gpu.texs[0].height;
        s_tex_use[s_tex_use_count].batches = 1;
        s_tex_use_count++;
        dump_texture_bmp((uint32_t)s_tex_use_count - 1);
    }
}

static void note_unhandled(uint32_t method, uint32_t param)
{
    int i;

    /* By index, as nv2a_pb_scan.c's note(): this runs for most words of the
     * stream. The report sorts s_unhandled in place, so the index is kept as
     * method -> entry by a search only when the cached slot no longer holds
     * that method. */
    static uint16_t slot[0x800];
    uint16_t *at = &slot[(method >> 2) & 0x7FFu];

    s_gpu.unhandled_total++;
    if (*at && s_unhandled[*at - 1].method == method) {
        s_unhandled[*at - 1].count++;
        s_unhandled[*at - 1].last_param = param;
        return;
    }
    for (i = 0; i < s_unhandled_count; i++) {
        if (s_unhandled[i].method == method) {
            s_unhandled[i].count++;
            s_unhandled[i].last_param = param;
            *at = (uint16_t)(i + 1);
            return;
        }
    }
    if (s_unhandled_count < PB_EXEC_MAX_UNHANDLED) {
        *at = (uint16_t)(s_unhandled_count + 1);
        s_unhandled[s_unhandled_count].method = method;
        s_unhandled[s_unhandled_count].count = 1;
        s_unhandled[s_unhandled_count].last_param = param;
        s_unhandled_count++;
    }
}

/* Read attribute `a` of vertex `index` as floats. Only the float and the
 * normalised-byte types appear in practice; anything else returns 0 so a
 * caller sees a degenerate vertex rather than reading past the array. */
/* RECOMP_PB_BATCH_DUMP=<flip>[,<flip>...]: which frames get a per-batch dump.
 * A list, so one run can follow an animation across frames. */
/* The render-state block of the 3D class (0x300..0x3FC: alpha test, blend,
 * depth, colour mask, stencil), as last set on subchannel 0. Only read by the
 * batch dump, to say how a batch was meant to be combined with what is under
 * it. */
static uint32_t s_rs[64];
#define RS(m) s_rs[((m) - 0x300) / 4]
/* The register combiners, for the batch dump: alpha input words and the
 * specular/fog pair (0x260-0x28C), factors and alpha/colour words
 * (0xA60-0xAFC), colour output words, control and the texture shader
 * (0x1E40-0x1E7C). The D3D11 path models none of it -- every stage is
 * texture times diffuse -- and this says what a batch asked for instead. */
static uint32_t s_cmb_a[12], s_cmb_b[40], s_cmb_c[16];
static uint16_t s_cmb_c_known;
/* The final combiner's two constants (SPECULAR_FOG_FACTOR, 0x1E20/0x1E24). */
static uint32_t s_cmb_fc[2];

/* getenv is slow in the debug CRT -- it locks and copies -- and this was
 * asked on every method and every draw: the largest single cost of the GPU
 * executor in a fight (58 of 34 samples on its thread, some twice). */
/* A host whose presents are paced by the display (vsync) makes the display
 * the frame clock: this timer then only has to stop the title running away
 * when the display is not pacing (a minimised or covered window presents at
 * once). 0 returns to RECOMP_FLIP_HZ. */
static volatile LONG s_flip_hz_override;
void xbox_Nv2aSetFlipHz(int hz) { InterlockedExchange(&s_flip_hz_override, hz > 0 ? hz : 0); }

static void flip_pace(void)
{
    static int env_hz = -1;
    int hz;
    static LARGE_INTEGER freq, next;
    LARGE_INTEGER now;
    LONGLONG period;

    if (env_hz < 0) {
        const char *e = getenv("RECOMP_FLIP_HZ");
        env_hz = e && *e ? atoi(e) : 60;
        QueryPerformanceFrequency(&freq);
    }
    hz = env_hz;
    if (hz > 0 && s_flip_hz_override)
        hz = (int)s_flip_hz_override;
    if (hz <= 0)
        return;
    period = freq.QuadPart / hz;
    QueryPerformanceCounter(&now);
    if (next.QuadPart && now.QuadPart < next.QuadPart) {
        LONGLONG left_ms = (next.QuadPart - now.QuadPart) * 1000 / freq.QuadPart;
#if defined(_WIN32)
        if (left_ms > 2)
            Sleep((DWORD)(left_ms - 1));
#else
        /* Sleep to within 0.3 ms of the deadline and spin only for that: the old 1 to 2 ms of
         * spinning was a core at full tilt for a tenth of every frame, on a chip whose
         * power the GPU shares. */
        if (left_ms >= 1) {
            LONGLONG left_ns = (next.QuadPart - now.QuadPart) * 1000000000LL / freq.QuadPart - 300000;
            if (left_ns > 0) {
                struct timespec ts = { (time_t)(left_ns / 1000000000LL), (long)(left_ns % 1000000000LL) };
                nanosleep(&ts, NULL);
            }
        }
#endif
        do {
            QueryPerformanceCounter(&now);
        } while (now.QuadPart < next.QuadPart);
    }
    /* From the later of the deadline and now: a late frame does not buy the
     * next ones a burst to catch up. */
    next.QuadPart = (now.QuadPart > next.QuadPart + period ? now.QuadPart : next.QuadPart) + period;
}

static int pb_exec_verbose(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("RECOMP_PB_EXEC_VERBOSE") != NULL;
    return on;
}

static uint32_t s_dump_requested;
/* SET_POINT_SIZE (0x043C), in pixels: the register is 6.3 fixed point. */
static float s_point_size = 1.0f;
/* The batch being collected came by DRAW_ARRAYS (for the batch dump). */
static int s_draw_arrays;

static int batch_dump_flip(uint32_t flip)
{
    static uint32_t flips[16];
    static int n = -1;
    int i;
    if (n < 0) {
        const char *s = getenv("RECOMP_PB_BATCH_DUMP");
        n = 0;
        while (s && *s && n < 16) {
            char *end;
            flips[n++] = (uint32_t)strtoul(s, &end, 0);
            s = (*end == ',') ? end + 1 : "";
        }
    }
    for (i = 0; i < n; i++)
        if (flips[i] == flip)
            return 1;
    return s_dump_requested && flip == s_dump_requested;
}

/* RECOMP_PB_BATCH_DUMP=shot: dump the frame after each back-buffer capture
 * (the host calls this when it takes one), so a capture aimed by time has its
 * batches beside it even though frame numbers differ from run to run. */
void nv2a_pb_request_dump_next(void)
{
    static int on = -1;
    if (on < 0) {
        const char *s = getenv("RECOMP_PB_BATCH_DUMP");
        on = s && !strcmp(s, "shot");
    }
    if (on) {
        s_dump_requested = s_gpu.flips + 1;
        fprintf(stderr, "  [BATCH] dumping flip %u (after a capture)\n", s_dump_requested);
    }
}

/* Bounded command-state history for a post-Present diagnostic. Own work:
 * djultra64. No vertex execution, GPU readback or printing while recording. */
#define PB_HISTORY_CAPACITY 2048u
enum { PB_HISTORY_TARGET=1,PB_HISTORY_CLEAR,PB_HISTORY_DRAW,PB_HISTORY_PRESENT };
typedef struct {
    uint64_t sequence;
    uint32_t kind,flip,draw,method,param,routed;
    uint32_t color,zeta,format,pitch,zeta_pitch,clip_x,clip_y,clip_w,clip_h;
    uint32_t primitive,indices,transform,clear,zclear;
    uint32_t clear_rect_h,clear_rect_v,clear_rect_known;
    uint32_t fog_enable,fog_mode,fog_color,fog_gen;float fog_param[2];
    uint32_t blend,src,dst,equation,mask,depth,zfunc,zwrite,stencil,sfunc,atest,afunc,aref;
    Texture textures[4];
    uint32_t texture_control[4],cull,cull_face,front_face;
    /* Diagnostic snapshot belongs to this draw, not the later dump. djultra64. */
    uint32_t vsh_hash, vsh_constant_version;
    float viewport_scale[4], viewport_offset[4];
    Nv2aCombiner combiner;
} PbHistoryRecord;
static PbHistoryRecord *s_history;
static uint64_t s_history_sequence,s_history_frame_start;
static int history_on(void)
{
    static int enabled=-1;
    if(enabled<0) {
        const char*e=getenv("RECOMP_PB_HISTORY");
        enabled=e && *e && strcmp(e,"0");
        if(enabled) {
            s_history=calloc(PB_HISTORY_CAPACITY,sizeof *s_history);
            if(!s_history) {enabled=0;fputs("[PB-HISTORY] allocation failed; disabled\n",stderr);}
        }
    }
    return enabled;
}
static void history_record(uint32_t kind,uint32_t method,uint32_t param,int routed)
{
    if(!history_on())return;
    uint64_t seq=s_history_sequence++;
    PbHistoryRecord*r=&s_history[seq%PB_HISTORY_CAPACITY];
    r->sequence=seq;r->kind=kind;r->flip=s_gpu.flips;r->draw=s_gpu.draws+((kind==PB_HISTORY_DRAW && routed)?1u:0u);
    r->method=method;r->param=param;r->routed=(uint32_t)routed;
    r->color=dma_resolve(s_gpu.color_offset);r->zeta=dma_resolve(s_gpu.zeta_offset);
    r->format=s_gpu.format;r->pitch=s_gpu.pitch;r->zeta_pitch=s_gpu.zeta_pitch;
    r->clip_x=s_gpu.clip_x;r->clip_y=s_gpu.clip_y;r->clip_w=s_gpu.clip_w;r->clip_h=s_gpu.clip_h;
    r->primitive=s_gpu.prim;r->indices=s_gpu.idx_count;r->transform=s_gpu.xf_mode;
    r->clear=s_gpu.clear_color;r->zclear=s_gpu.zstencil_clear;
    r->clear_rect_h=s_gpu.clear_rect_h;r->clear_rect_v=s_gpu.clear_rect_v;r->clear_rect_known=s_gpu.clear_rect_known;
    r->fog_enable=s_gpu.fog_enable;r->fog_mode=s_gpu.fog_mode;r->fog_color=s_gpu.fog_color;r->fog_gen=s_gpu.fog_gen;memcpy(r->fog_param,s_gpu.fog_param,sizeof r->fog_param);
    r->blend=s_gpu.blend_enable;r->src=s_gpu.blend_sfactor;r->dst=s_gpu.blend_dfactor;
    r->equation=s_gpu.blend_equation;r->mask=s_gpu.color_mask;
    r->depth=s_gpu.depth_test;r->zfunc=s_gpu.depth_func;r->zwrite=s_gpu.depth_mask;
    r->stencil=RS(0x32C);r->sfunc=RS(0x364);r->atest=s_gpu.alpha_test;
    r->afunc=s_gpu.alpha_func;r->aref=s_gpu.alpha_ref;
    memcpy(r->textures,s_gpu.texs,sizeof r->textures);
    memcpy(r->texture_control,s_gpu.texture_control,sizeof r->texture_control);
    r->cull=RS(0x308);r->cull_face=RS(0x39C);r->front_face=RS(0x3A0);
    r->combiner=s_gpu.rc;
    if(kind==PB_HISTORY_DRAW) {
        const float *constants=nv2a_vsh_constants(&r->vsh_constant_version);
        r->vsh_hash=nv2a_vsh_program_hash();
        memcpy(r->viewport_scale,constants+58*4,sizeof r->viewport_scale);
        memcpy(r->viewport_offset,constants+59*4,sizeof r->viewport_offset);
    }
}
/* Call synchronously inside the host's post-Present callback. The frame's
 * boundary advances only AFTER that callback returns, so this includes the
 * commands of the image just observed, not the following GPU frame. */
void nv2a_pb_dump_present_history(uint64_t host_frame,uint64_t elapsed_ns)
{
    if(!history_on())return;
    uint64_t end=s_history_sequence;
    uint64_t earliest=end>PB_HISTORY_CAPACITY?end-PB_HISTORY_CAPACITY:0;
    uint64_t begin=s_history_frame_start>earliest?s_history_frame_start:earliest;
    uint64_t lost=begin-s_history_frame_start;
    fprintf(stderr,"[PB-HISTORY] begin host_frame=%llu elapsed_ns=%llu gpu_flip=%u sequence=%llu..%llu capacity=%u record_bytes=%zu lost=%llu\n",
        (unsigned long long)host_frame,(unsigned long long)elapsed_ns,s_gpu.flips,
        (unsigned long long)begin,(unsigned long long)end,PB_HISTORY_CAPACITY,sizeof *s_history,(unsigned long long)lost);
    static const char*names[]={"INVALID","TARGET","CLEAR","DRAW","PRESENT"};
    for(uint64_t seq=begin;seq<end;seq++) {
        const PbHistoryRecord*r=&s_history[seq%PB_HISTORY_CAPACITY];
        fprintf(stderr,"[PB-HISTORY] seq=%llu gpu_flip=%u draw=%u kind=%s method=%04X param=%08X routed=%u color=%08X zeta=%08X format=%08X pitch=%u/%u clip=%ux%u+%u+%u prim=%u n=%u xf=%u clear=%08X zclear=%08X\n",
            (unsigned long long)r->sequence,r->flip,r->draw,names[r->kind],r->method,r->param,r->routed,
            r->color,r->zeta,r->format,r->pitch,r->zeta_pitch,r->clip_w,r->clip_h,r->clip_x,r->clip_y,
            r->primitive,r->indices,r->transform,r->clear,r->zclear);
        if(r->kind==PB_HISTORY_CLEAR)
            fprintf(stderr,"[PB-HISTORY] clearrect seq=%llu horizontal=%08X vertical=%08X known=%u\n",
                (unsigned long long)seq,r->clear_rect_h,r->clear_rect_v,r->clear_rect_known);
        if(r->kind!=PB_HISTORY_DRAW)continue;
        fprintf(stderr,"[PB-HISTORY] fog seq=%llu enable=%u mode=%X color_abgr=%08X params=%a,%a gen=%u\n",(unsigned long long)seq,r->fog_enable,r->fog_mode,r->fog_color,(double)r->fog_param[0],(double)r->fog_param[1],r->fog_gen);
        fprintf(stderr,"[PB-HISTORY] viewport seq=%llu vsh_hash=%08X constant_version=%u c58=%a,%a,%a,%a c59=%a,%a,%a,%a\n",
            (unsigned long long)seq,r->vsh_hash,r->vsh_constant_version,
            (double)r->viewport_scale[0],(double)r->viewport_scale[1],(double)r->viewport_scale[2],(double)r->viewport_scale[3],
            (double)r->viewport_offset[0],(double)r->viewport_offset[1],(double)r->viewport_offset[2],(double)r->viewport_offset[3]);
        const Nv2aCombiner*c=&r->combiner;
        fprintf(stderr,"[PB-HISTORY] state seq=%llu blend=%u %X/%X eq=%X mask=%08X depth=%u func=%X write=%u stencil=%u func=%X atest=%u func=%X ref=%X combiner=%08X stages=%05X final=%08X/%08X constants=%08X/%08X cull=%u face=%X front=%X\n",
            (unsigned long long)seq,r->blend,r->src,r->dst,r->equation,r->mask,r->depth,r->zfunc,r->zwrite,
            r->stencil,r->sfunc,r->atest,r->afunc,r->aref,c->control,c->stage_program,c->final0,c->final1,c->final_c0,c->final_c1,r->cull,r->cull_face,r->front_face);
        for(unsigned st=0;st<4;st++) {
            const Texture*t=&r->textures[st];
            fprintf(stderr,"[PB-HISTORY] tex seq=%llu stage=%u addr=%08X format=%02X size=%ux%u pitch=%u valid=%u disabled=%d levels=%u filter=%08X control=%08X min_lod=%u max_lod=%u address=%X/%X cube=%d\n",
                (unsigned long long)seq,st,t->offset,t->color,t->width,t->height,t->pitch,t->valid,t->disabled,t->levels,t->filter,r->texture_control[st],(r->texture_control[st]>>18)&0xfffu,(r->texture_control[st]>>6)&0xfffu,t->addr_u,t->addr_v,t->cube);
        }
        for(unsigned st=0;st<(c->control&255u)&&st<8;st++)
            fprintf(stderr,"[PB-HISTORY] cmb seq=%llu stage=%u icw=%08X/%08X ocw=%08X/%08X c=%08X/%08X\n",
                (unsigned long long)seq,st,c->color_icw[st],c->alpha_icw[st],c->color_ocw[st],c->alpha_ocw[st],c->factor0[st],c->factor1[st]);
    }
    fprintf(stderr,"[PB-HISTORY] end host_frame=%llu gpu_flip=%u lost=%llu\n",
        (unsigned long long)host_frame,s_gpu.flips,(unsigned long long)lost);
}
static void history_present_complete(void)
{
    if(history_on())s_history_frame_start=s_history_sequence;
}

static int fetch_attr(const VertexAttr *a, uint32_t index, float out[4])
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    const uint8_t *p;
    uint32_t i;

    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    if (!a->size || !a->stride)
        return 0;
    if (s_gpu.inline_active) {
        /* The batch arrived as INLINE_ARRAY, so `offset` is a byte offset into
         * the buffered payload rather than a guest address -- and 0 is a legal
         * one there, which is why the offset test is on the other side of this
         * branch. */
        size_t at = (size_t)a->offset + (size_t)index * a->stride;
        size_t components = a->size < 4 ? a->size : 4;
        size_t bytes, available = (size_t)s_gpu.inline_count * 4;
        /* Validate every byte the decoder below will read, not just the
         * first dword. A float4 requires 16 bytes, a short3 requires 6. */
        switch (a->type) {
        case 0: case 6: bytes = 4; break;
        case 2: bytes = components * 4; break;
        case 4: bytes = components; break;
        case 1: case 5: bytes = components * 2; break;
        default: return 0;
        }
        if (at > available || bytes > available - at)
            return 0;
        p = (const uint8_t *)s_gpu.inline_buf + at;
    } else {
        /* The low window or the contiguous one, nothing else. The attribute
         * pointer is the title's, taken from a push buffer, and one stale
         * pointer (seen once, 0xBD104F57, a minute after START on Def Jam's
         * title) otherwise takes the whole process down from the GPU thread.
         * A vertex that cannot be read is absent, like one with no array. */
        uint64_t va = (uint64_t)a->offset + (uint64_t)index * a->stride;
        if (!a->offset
         || !((va >= 0x1000u && va + 16 <= 0x04000000u)
           || (va >= 0x80000000u && va + 16 <= 0x84000000u))) {
            static int warned;
            if (a->offset && !warned) {
                warned = 1;
                fprintf(stderr, "  [GPU] vertex attribute outside guest memory: offset "
                                "0x%08X index %u stride %u; reading it as absent\n",
                        a->offset, index, a->stride);
            }
            return 0;
        }
        p = mem + va;
    }

    switch (a->type) {
    case 0:                                  /* D3DCOLOR */
        /* A DWORD 0xAARRGGBB, so little-endian bytes are B,G,R,A -- not the
         * component order of every other format here. Returned as R,G,B,A so
         * callers need not know which format the title chose. */
        out[0] = (float)p[2] / 255.0f;
        out[1] = (float)p[1] / 255.0f;
        out[2] = (float)p[0] / 255.0f;
        out[3] = (float)p[3] / 255.0f;
        return 1;
    case 2:                                  /* float */
        /* FLOAT3 dominates captured Top Spin geometry. Keep ordered reads
         * and the existing decoder/bounds for every other component count.
         * Own work: djultra64. */
        if (a->size == 3) {
            out[0] = ((const float *)p)[0];
            out[1] = ((const float *)p)[1];
            out[2] = ((const float *)p)[2];
            return 1;
        }
        for (i = 0; i < a->size && i < 4; i++)
            out[i] = ((const float *)p)[i];
        return 1;
    case 4:                                  /* unsigned byte, normalised */
        for (i = 0; i < a->size && i < 4; i++)
            out[i] = (float)p[i] / 255.0f;
        return 1;
    /* The formats 3D geometry uses and screen-space quads never did. */
    case 1:                                  /* signed short, normalised */
        for (i = 0; i < a->size && i < 4; i++)
            out[i] = (float)((const int16_t *)p)[i] / 32767.0f;
        return 1;
    case 5:                                  /* signed short, as is */
        for (i = 0; i < a->size && i < 4; i++)
            out[i] = (float)((const int16_t *)p)[i];
        return 1;
    case 6: {                                /* packed 11:11:10 normal */
        uint32_t v = *(const uint32_t *)p;
        out[0] = (float)((int32_t)(v << 21) >> 21) / 1023.0f;
        out[1] = (float)((int32_t)(v << 10) >> 21) / 1023.0f;
        out[2] = (float)((int32_t)v >> 22) / 511.0f;
        return 1;
    }
    default:
        return 0;
    }
}

/* Widescreen 2D placement (docs/widescreen in the Def Jam port).
 *
 * With a widescreen canvas the title's 640-wide coordinates are spread over a
 * wider target and the title widens its own 3D view. Its 2D -- menus, HUD,
 * movies -- would come out stretched, so each 2D batch is placed here instead:
 * squeezed by k = (title aspect) / (canvas aspect) and centred; during a fight
 * (the host's heartbeat) HUD pieces wholly left or right of the middle are
 * anchored to that side; full-screen fills (fades, tints, vignettes) stretch.
 * A batch is 2D when its first vertices come out with w = 1 and one depth
 * (an orthographic view); 3D batches are left alone. k = 1 turns it all off. */
static volatile float s_ws_k = 1.0f, s_ws_reach = 1.0f;
static volatile int s_ws_side_fill = 1;
static volatile uint32_t s_ws_hud_flip = 0xFFFFFFF0u;
static volatile int s_ws_hud_bottom;     /* 3-4 fighters: HUD blocks in the bottom corners too */
static int s_ws_frame_3d, s_ws_frame_pic;  /* this frame drew 3D / centred a full-screen picture */
static uint32_t s_ws_counts[6];

void nv2a_ws_configure(float k, float hud_reach)
{
    s_ws_k = (k > 0.0f && k <= 1.0f) ? k : 1.0f;
    s_ws_reach = (hud_reach > 0.0f && hud_reach <= 1.0f) ? hud_reach : 1.0f;
}

/* 1: a full-screen picture that is centred (movie, poster) gets a blurred
 * extension beside it at present; 0: black. */
void nv2a_ws_set_side_fill(int on) { s_ws_side_fill = on ? 1 : 0; }

/* Call once per game tick while a fight's HUD is up; bottom_rows when 3 or 4
 * fighters are in the match (their HUD blocks sit in the bottom corners). */
void nv2a_ws_hud_heartbeat(int bottom_rows)
{
    s_ws_hud_bottom = bottom_rows;
    s_ws_hud_flip = s_gpu.flips;
}

/* End of a frame: the side fill goes beside a centred full-screen picture only
 * on a frame with no 3D at all (a movie, a loading poster, a 2D menu). Fights,
 * cutscenes and menus over a 3D scene keep their real sides. Lands at the next
 * present, a frame later at most. */
static void ws_end_frame(void)
{
    if (s_ws_k < 1.0f && s_ws_side_fill && s_ws_frame_pic && !s_ws_frame_3d) {
        extern void d3d8_RequestSideFill(void);
        d3d8_RequestSideFill();
    }
    s_ws_frame_3d = s_ws_frame_pic = 0;
}

static int ws_vertex(int vsh, uint32_t used, uint32_t vi, float out[4])
{
    if (vsh) {
        float in[16][4];
        NvVshOutput o;
        uint32_t a;
        for (a = 0; a < 16; a++) {
            if (!((used >> a) & 1)) {
                in[a][0] = in[a][1] = in[a][2] = 0.0f;
                in[a][3] = 1.0f;
            } else if (!fetch_attr(&s_gpu.attr[a], vi, in[a])) {
                nv2a_vsh_current((int)a, in[a]);
            }
        }
        if (!nv2a_vsh_run_ready((const float (*)[4])in, &o))
            return 0;
        out[0] = o.pos[0]; out[1] = o.pos[1]; out[2] = o.pos[2]; out[3] = o.pos[3];
        return 1;
    }
    if (!fetch_attr(&s_gpu.attr[0], vi, out))
        return 0;
    out[3] = 1.0f;
    return 1;
}

/* Decide this batch's placement and hand it to the renderer. `ready` holds its
 * vertices when the CPU path transformed them (n of them), else NULL. */
static void ws_place_batch(int vsh, uint32_t used, const PgraphReadyVertex *ready, uint32_t n)
{
    extern void pgraph_d3d11_set_placement(float kx, float bx);
    float k = s_ws_k, x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, z = 0.0f, p[4];
    uint32_t i, cnt = ready ? n : s_gpu.idx_count;
    int cls;                               /* 0 3D, 1 centre, 2 left, 3 right, 4 stretch */

    if (k >= 1.0f)
        return;
    if (cnt > 512)
        cnt = 512;                         /* plenty to tell a 2D batch's extent */
    cls = 1;
    for (i = 0; i < cnt; i++) {
        if (ready) {
            p[0] = ready[i].x; p[1] = ready[i].y; p[2] = ready[i].z; p[3] = ready[i].w;
        } else if (!ws_vertex(vsh, used, s_gpu.idx[i], p)) {
            break;
        }
        if (i == 0)
            z = p[2];
        if (i < 3 && (fabsf(p[3] - 1.0f) > 1e-4f || fabsf(p[2] - z) > 1e-3f * (fabsf(z) + 1.0f))) {
            cls = 0;                       /* perspective or varying depth: 3D */
            break;
        }
        if (p[0] < x0) x0 = p[0];
        if (p[0] > x1) x1 = p[0];
        if (p[1] < y0) y0 = p[1];
        if (p[1] > y1) y1 = p[1];
    }
    if (cls && x0 <= 2.0f && x1 >= 638.0f && y0 <= 2.0f && y1 >= 478.0f) {
        /* Full screen: a fill stretches, a picture (movie, poster) is centred. */
        int tex = s_gpu.texs[0].valid && s_gpu.texs[0].width && s_gpu.texs[0].height;
        uint32_t big = tex ? (s_gpu.texs[0].width > s_gpu.texs[0].height ? s_gpu.texs[0].width
                                                                          : s_gpu.texs[0].height) : 0;
        cls = (!tex || big <= 128) ? 4 : 1;
        if (cls == 1)
            s_ws_frame_pic = 1;
    } else if (cls && (uint32_t)(s_gpu.flips - s_ws_hud_flip) <= 30u
               && (y1 <= 130.0f || (s_ws_hud_bottom && y0 >= 350.0f))) {
        /* Only the HUD's own strips: the top one, and the bottom one with 3-4
         * fighters. Tutorial prompts and other centred text stay whole. */
        if (x1 <= 300.0f)
            cls = 2;
        else if (x0 >= 340.0f)
            cls = 3;
    }
    s_ws_counts[cls]++;
    if (cls == 0)
        s_ws_frame_3d = 1;
    {
        float bx = cls == 2 ? k - s_ws_reach : cls == 3 ? s_ws_reach - k : 0.0f;
        if (cls == 1 || cls == 2 || cls == 3) {
            /* Clip the placed batch to where its own clip rectangle lands, so
             * what the title keeps off screen (slide-ins start far outside
             * 0..640) does not show in the new side areas: pixel x becomes
             * x * k + 320 * (1 - k + bx). */
            float off = 320.0f * (1.0f - k + bx);
            uint32_t cx = (uint32_t)(s_gpu.clip_x * k + off + 0.5f);
            uint32_t cw = (uint32_t)(s_gpu.clip_w * k + 0.5f);
            pgraph_d3d11_set_placement(k, bx);
            pgraph_d3d11_set_surface_clip(cx, s_gpu.clip_y, cw ? cw : 1, s_gpu.clip_h);
        } else {
            pgraph_d3d11_set_placement(1.0f, 0.0f);
        }
    }
    {
        static uint32_t last_flip;
        if (s_gpu.flips - last_flip >= 600u) {
            last_flip = s_gpu.flips;
            fprintf(stderr, "[WS] 2D placement: 3D %u, centred %u, left %u, right %u, stretched %u\n",
                    s_ws_counts[0], s_ws_counts[1], s_ws_counts[2], s_ws_counts[3], s_ws_counts[4]);
        }
    }
}

static uint32_t surface_bpp(void);

static void note_drawn(void)
{
    if (!s_gpu.drawn_stale && s_gpu.drawn_offset
        && s_gpu.clip_w * s_gpu.clip_h < s_gpu.drawn_w * s_gpu.drawn_h)
        return;
    s_gpu.drawn_stale = 0;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_gpu.drawn_pitch = s_gpu.pitch;
    s_gpu.drawn_bpp = surface_bpp();
    s_gpu.drawn_x = s_gpu.clip_x; s_gpu.drawn_y = s_gpu.clip_y;
    s_gpu.drawn_w = s_gpu.clip_w; s_gpu.drawn_h = s_gpu.clip_h;
}

static uint32_t surface_bpp(void)
{
    /* The surface format says it outright (NV097_SET_SURFACE_FORMAT_COLOR).
     * Deriving it from pitch / clip width -- the only way before -- is right
     * only while the clip spans the whole surface: Burnout 3 narrows the clip
     * to a 250-pixel window to draw its option values, 2560 / 250 came out as
     * 10 bytes a pixel, and every such text quad was refused. */
    switch (s_gpu.format & 0xF) {
    case 0x1: case 0x2: case 0x3: case 0xA:
        return 2;
    case 0x4: case 0x5: case 0x6: case 0x7: case 0x8:
        return 4;
    case 0x9:
        return 1;
    default:
        break;
    }
    /* The pitch and the clip width together give the pixel size, which is more
     * reliable than decoding the format field: the format's colour code is
     * only meaningful alongside a type the title also sets, while the pitch is
     * always exactly how many bytes a row occupies. */
    if (!s_gpu.clip_w)
        return 0;
    return s_gpu.pitch / s_gpu.clip_w;
}


/* Write the current surface out as a 24-bit BMP.
 *
 * A framebuffer window needs someone watching it. A file does not, which makes
 * this the only way to check what a title actually rendered on a machine you
 * are not sitting at -- and the only way to put a picture in a bug report.
 *
 * ponytail: bottom-up 24bpp BMP, no palette, no compression. That is the one
 * format every viewer reads and it is 30 lines; PNG would need a dependency.
 */
/* Set while RECOMP_FB_DUMP_FLIPS is dumping every flip: the report and
 * after-draw dumps stand down then, or they would put duplicate frames into
 * the sequence. */
static int s_flip_dumping;

static void dump_surface_bmp(void)
{
    const char *prefix = getenv("RECOMP_FB_DUMP");
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    int drawn = s_gpu.drawn_offset != 0;
    uint32_t offset = drawn ? s_gpu.drawn_offset : s_gpu.color_offset;
    uint32_t pitch = drawn ? s_gpu.drawn_pitch : s_gpu.pitch;
    uint32_t cx = drawn ? s_gpu.drawn_x : s_gpu.clip_x;
    uint32_t cy = drawn ? s_gpu.drawn_y : s_gpu.clip_y;
    static int seq;
    char path[512];
    uint32_t w = drawn ? s_gpu.drawn_w : s_gpu.clip_w;
    uint32_t h = drawn ? s_gpu.drawn_h : s_gpu.clip_h, y, x;
    uint32_t bpp = drawn ? s_gpu.drawn_bpp : surface_bpp();
    uint32_t row_bytes, pad, filesz;
    uint8_t hdr[54];
    FILE *f;

    if (!prefix || !w || !h || (bpp != 2 && bpp != 4) || !offset)
        return;

    row_bytes = w * 3;
    pad = (4 - (row_bytes & 3)) & 3;
    filesz = 54 + (row_bytes + pad) * h;

    snprintf(path, sizeof path, "%s%05d.bmp", prefix, seq++);
    f = fopen(path, "wb");
    if (!f)
        return;

    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &filesz, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, sizeof hdr, f);

    /* BMP rows run bottom-up. */
    for (y = h; y-- > 0; ) {
        const uint8_t *row = mem + dma_resolve(offset)
                           + (size_t)(cy + y) * pitch;
        for (x = 0; x < w; x++) {
            uint8_t bgr[3];
            if (bpp == 4) {
                uint32_t v = ((const uint32_t *)row)[cx + x];
                bgr[0] = (uint8_t)(v);
                bgr[1] = (uint8_t)(v >> 8);
                bgr[2] = (uint8_t)(v >> 16);
            } else {
                uint16_t v = ((const uint16_t *)row)[cx + x];
                bgr[0] = (uint8_t)(( v        & 0x1F) << 3);
                bgr[1] = (uint8_t)(((v >>  5) & 0x3F) << 2);
                bgr[2] = (uint8_t)(((v >> 11) & 0x1F) << 3);
            }
            fwrite(bgr, 1, 3, f);
        }
        if (pad) {
            static const uint8_t zero[3] = {0, 0, 0};
            fwrite(zero, 1, pad, f);
        }
    }
    fclose(f);
    if (seq == 1)
        fprintf(stderr, "  [GPU] framebuffer dump: %s (%ux%u from 0x%08X %ubpp)\n",
                path, w, h, s_gpu.color_offset, bpp);
}

/* Defined below, next to the rest of the rasteriser; the clear path uses it
 * for RECOMP_RASTER_TEST. */
static void raster_triangle(const float a[2], const float b[2],
                            const float c[2], uint32_t argb,
                            const float uv[3][2]);

/* With the D3D11 translator drawing, the software rasteriser's writes into
 * the title's framebuffer are redundant, and a surface decoded from a
 * misread command lets them land anywhere in guest memory -- the push buffer
 * included. Off then, unless RECOMP_PB_SOFT_RASTER=1. */
static int soft_raster_on(void)
{
    static int on = -1;
    if (on < 0)
        on = !getenv("RECOMP_PB_D3D11") || getenv("RECOMP_PB_SOFT_RASTER") != NULL;
    return on;
}

static void clear_surface(uint32_t param)
{
    uint8_t *mem = (uint8_t *)xbox_GetMemoryOffset();
    uint32_t bpp = surface_bpp();
    uint32_t y, x;

    if (!(param & NV097_CLEAR_COLOR_MASK) || !soft_raster_on())
        return;                            /* depth/stencil only, or not drawing here */
    if (!s_gpu.color_offset || !s_gpu.pitch || !s_gpu.clip_h || bpp == 0)
        return;
    {
        uint32_t base = dma_resolve(s_gpu.color_offset);
        if (surface_write_refused(base,
                                  (s_gpu.clip_y + s_gpu.clip_h) * s_gpu.pitch,
                                  "clear"))
            return;
        s_gpu.color_base = base;
    }

    for (y = 0; y < s_gpu.clip_h; y++) {
        uint8_t *row = mem + s_gpu.color_base
                     + (size_t)(s_gpu.clip_y + y) * s_gpu.pitch;
        if (bpp == 4) {
            uint32_t *p = (uint32_t *)row + s_gpu.clip_x;
            for (x = 0; x < s_gpu.clip_w; x++)
                p[x] = s_gpu.clear_color;
        } else if (bpp == 2) {
            /* The clear value is always given as A8R8G8B8; a 16-bit surface
             * takes the same colour reduced to 5:6:5. */
            uint16_t v = (uint16_t)(((s_gpu.clear_color >> 8) & 0xF800)
                                  | ((s_gpu.clear_color >> 5) & 0x07E0)
                                  | ((s_gpu.clear_color >> 3) & 0x001F));
            uint16_t *p = (uint16_t *)row + s_gpu.clip_x;
            for (x = 0; x < s_gpu.clip_w; x++)
                p[x] = v;
        }
    }
    s_gpu.clears++;
    /* Progress markers, interleaved with everything else in the log. The
     * summary says drawing stopped; only a marker next to the surrounding
     * activity says what the title was doing when it stopped. */
    if ((s_gpu.clears % 100) == 0 && !runtime_quiet_enabled())
        fprintf(stderr, "  [GPU] clear #%u\n", s_gpu.clears);
    /* Distinct clear colours actually used. "Cleared to black" and "the clear
     * never ran" look identical in the framebuffer, and only one of them is a
     * bug -- so record what was asked for, not just how often. */
    {
        static uint32_t seen[8];
        static int n;
        int i;
        for (i = 0; i < n; i++)
            if (seen[i] == s_gpu.clear_color) break;
        if (i == n && n < 8) {
            seen[n++] = s_gpu.clear_color;
            fprintf(stderr, "  [GPU] clear colour 0x%08X -> surface 0x%08X"
                            " (%ubpp)\n",
                    s_gpu.clear_color, s_gpu.color_offset, surface_bpp());
        }
    }

    /* Prove the pixel path end to end, independent of whether the title has
     * given us any geometry yet.
     *
     * "Nothing on screen" has three very different causes -- the surface
     * address or pitch is wrong, the rasteriser is broken, or the title's
     * vertex buffers are empty -- and they are indistinguishable from a black
     * window. RECOMP_RASTER_TEST draws one known triangle into the surface
     * just cleared, so a visible triangle rules out the first two and leaves
     * only the third. On Wreckless it is the third: attribute 0 decodes
     * correctly (float, size 2, stride 16) and the buffer it points at stays
     * zero.
     *
     * ponytail: bring-up aid, not a feature. It costs one branch per clear. */
    static int raster_test = -1;
    if (raster_test < 0)
        raster_test = getenv("RECOMP_RASTER_TEST") != NULL;
    if (raster_test) {
        static int announced;
        /* Every clear, not once: the title clears each frame and double-buffers,
         * so a triangle drawn a single time is erased before anyone sees it. */
        if (s_gpu.clip_w && s_gpu.clip_h) {
            float a[2], b[2], c[2];
            a[0] = s_gpu.clip_w * 0.5f; a[1] = s_gpu.clip_h * 0.15f;
            b[0] = s_gpu.clip_w * 0.85f; b[1] = s_gpu.clip_h * 0.85f;
            c[0] = s_gpu.clip_w * 0.15f; c[1] = s_gpu.clip_h * 0.85f;
            raster_triangle(a, b, c, 0xFFFF00FFu, NULL);  /* magenta: never a clear colour */
            if (announced++ == 0)
            fprintf(stderr, "  [GPU] raster self-test: triangle (%.0f,%.0f)"
                            " (%.0f,%.0f) (%.0f,%.0f) into 0x%08X %ubpp\n",
                    a[0], a[1], b[0], b[1], c[0], c[1],
                    s_gpu.color_offset, surface_bpp());
        }
    }

    /* Show the surface actually being drawn into. A title that double-buffers
     * renders into the back buffer, so following AvSetDisplayMode's address
     * would show the one nothing is writing. */
    /* The window has to read where the pixels actually are, which is the
     * resolved address rather than the DMA-object offset. */
    /* Only until the title flips. Following the draw surface on every scan
     * shows the buffer being written right now, half a frame at a time; past
     * the first flip the window is repointed at the finished one instead. */
    if (s_gpu.flips == 0)
        xbox_FramebufferWindowSet(dma_resolve(s_gpu.color_offset), s_gpu.pitch);

    /* And open the window, rather than waiting for AvSetDisplayMode to do it.
     *
     * That was the only caller, so a title which draws before setting a display
     * mode -- or never sets one at all -- got no window however much it
     * rendered. The Xbox Dashboard clears a 1280x960 surface at 0x00088000 on
     * its first frame and had not called AvSetDisplayMode by then, so
     * RECOMP_FB_WINDOW=1 was set, the executor knew the address and the pitch,
     * and nothing appeared.
     *
     * Here is the better trigger anyway: this runs when a surface address is
     * known to be real, because a clear just used it. Idempotent and gated on
     * RECOMP_FB_WINDOW, so the cost is one interlocked compare per clear. */
    xbox_FramebufferWindowStart();
}


/* ── Rasteriser ──────────────────────────────────────────────────────────
 *
 * Fills triangles straight into the guest framebuffer, the same memory
 * clear_surface() writes and the framebuffer window already shows. That is the
 * whole reason it is done on the CPU rather than through D3D: nothing new has
 * to be plumbed for the result to be visible.
 *
 * ponytail: flat-shaded, no depth buffer, no texturing, no perspective
 * correction, and only batches whose attribute 0 is already in screen space.
 * A title running a vertex program hands over object-space positions that mean
 * nothing without executing the program, so those batches are counted and
 * skipped rather than drawn somewhere wrong. Upgrade path is the D3D11
 * translator in src/nv2a/nv2a_pgraph_d3d11.c once vertex programs are
 * translated; this exists to get the first geometry on screen for every title,
 * which in practice is UI, HUD and 2D overlays -- all pre-transformed.
 */

/* One texel, in the title's own format.
 *
 * The codes are the NV097 colour field, which is the Xbox D3DFMT_ enum --
 * src/d3d/d3d8_xbox.h is the table, and it is the table to check against
 * rather than recollection: 0x1E is LIN_X8R8G8B8 and not, as this first read
 * it, a byte-reversed BGRA. Getting that one wrong turned an opaque black
 * render target into a screen of pure blue, which is the kind of wrong that
 * looks like content.
 *
 * Only the linear (LIN_) formats are read. A swizzled texture stores its
 * texels in Morton order rather than in rows, so reading one as if it had a
 * pitch does not give a slightly wrong colour, it gives a different image --
 * and inventing that image is exactly what this is not for. An unsupported
 * format samples nothing and the caller keeps the vertex colour, which is
 * visibly wrong rather than quietly wrong.
 *
 * ponytail: nearest texel, no filtering, whatever SET_TEXTURE_FILTER asked
 * for. Bilinear when a title's output actually depends on it.
 */
/* Off the edge of the texture, the way the title asked for.
 *
 * Refusing to sample instead is not neutral: it hands the caller back the
 * vertex colour, so a pass whose coordinates reach the last texel by half a
 * texel gets a bright line down the edge of the screen. The dashboard's
 * resolve does exactly that -- its last column and last row, 1119 pixels of
 * white on a black frame, from a rounding step at the boundary.
 */
static uint32_t wrap_coord(uint32_t c, uint32_t size, uint32_t mode)
{
    /* Coordinates are signed: a bilinear tap one texel left of or above
     * texel 0 is -1, which must wrap to the far edge or clamp to 0 -- as an
     * unsigned value it clamped to the far edge instead. */
    int32_t sc = (int32_t)c, n = (int32_t)size;
    if (!size)
        return 0;
    if (mode == 1)                         /* wrap */
        return (uint32_t)(((sc % n) + n) % n);
    return sc < 0 ? 0u : (sc >= n ? size - 1 : c);   /* clamp, and the rest */
}

static uint32_t expand(uint32_t v, uint32_t bits)
{
    return d3d8_expand_channel(v, bits);
}

/* The linear format that decodes the same texels as a swizzled one.
 *
 * Swizzling changes where a texel lives, not what it says: A8R8G8B8 (0x06) and
 * LIN_A8R8G8B8 (0x12) are the same four bytes in the same order. So the whole
 * difference is the address calculation, and one of those lets every format
 * below serve both. Pairs read off the table in d3d8_xbox.h rather than
 * recalled -- the comment above this one is about getting exactly that wrong. */
static uint32_t linear_twin(uint32_t fmt)
{
    switch (fmt) {
    case 0x00: return 0x13;                /* L8        -> LIN_L8        */
    case 0x02: return 0x10;                /* A1R5G5B5  -> LIN_A1R5G5B5  */
    case 0x03: return 0x1C;                /* X1R5G5B5  -> LIN_X1R5G5B5  */
    case 0x04: return 0x1D;                /* A4R4G4B4  -> LIN_A4R4G4B4  */
    case 0x05: return 0x11;                /* R5G6B5    -> LIN_R5G6B5    */
    case 0x06: return 0x12;                /* A8R8G8B8  -> LIN_A8R8G8B8  */
    case 0x07: return 0x1E;                /* X8R8G8B8  -> LIN_X8R8G8B8  */
    case 0x19: return 0x1F;                /* A8        -> LIN_A8        */
    default:   return fmt;                 /* already linear, or unhandled */
    }
}

/* Sample texel (u, v) of stage texture `t`; `face` offsets a cube map. */
static int sample_tex(const Texture *t, uint32_t face_offset,
                      uint32_t u, uint32_t v, uint32_t *argb)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    const uint8_t *p;
    uint32_t fmt, base = t->offset + face_offset;

    if (!t->valid)
        return 0;
    u = wrap_coord(u, t->width,  t->addr_u);
    v = wrap_coord(v, t->height, t->addr_v);

    fmt = t->color;
    if (d3d8_format_dxt_block_bytes(fmt)) {
        /* Decoded blocks, direct-mapped by address. Every texel of a DXT
         * texture decodes its whole 4x4 block, and bilinear reads four
         * neighbours that nearly always share one -- this was a tenth of
         * the executor's time. Keyed on the block's bytes as well as its
         * address, so a texture rewritten in place (a render target reused,
         * a streamed mip) never returns stale texels.
         * ponytail: single-threaded executor, so a plain static table. */
        static NV_TLS struct { uintptr_t key; uint32_t fmt; uint64_t raw[2];
                        uint32_t px[16]; } cache[4096];
        uint32_t bb = d3d8_format_dxt_block_bytes(fmt);
        uint32_t bx = u >> 2, by = v >> 2, bw = (t->width + 3) >> 2;
        const uint8_t *blk = mem + base + ((size_t)by * bw + bx) * bb;
        uintptr_t key = (uintptr_t)blk;
        size_t slot = ((key / bb) ^ (key >> 16)) & 4095;
        uint64_t raw[2] = {0, 0};
        memcpy(raw, blk, bb);
        if (cache[slot].key != key || cache[slot].fmt != fmt
            || cache[slot].raw[0] != raw[0] || cache[slot].raw[1] != raw[1]) {
            uint32_t i;
            for (i = 0; i < 16; i++)
                d3d8_dxt_decode_texel(mem + base, fmt, bx * 4 + (i & 3),
                                      by * 4 + (i >> 2), t->width,
                                      &cache[slot].px[i]);
            cache[slot].key = key;
            cache[slot].fmt = fmt;
            cache[slot].raw[0] = raw[0];
            cache[slot].raw[1] = raw[1];
        }
        *argb = cache[slot].px[(v & 3) * 4 + (u & 3)];
        return 1;
    }
    if (d3d8_format_is_swizzled(fmt)) {
        /* Morton order: a texel's index is interleaved from x and y instead of
         * v*pitch + u, so index from the base of the image. The switch below
         * casts to each format's own width, which makes that index a texel
         * index for every one of them. */
        fmt = linear_twin(fmt);
        p = mem + base;
        u = swizzle_offset(u, v, t->width, t->height);
    } else {
        p = mem + base + (size_t)v * t->pitch;
    }

    switch (fmt) {

    /* 32-bit, alpha-red-green-blue in the dword. */
    case 0x12:                                      /* LIN_A8R8G8B8 */
        *argb = ((const uint32_t *)p)[u];
        return 1;
    case 0x1E:                                      /* LIN_X8R8G8B8 */
        *argb = ((const uint32_t *)p)[u] | 0xFF000000u;
        return 1;

    /* 8-bit palette index, swizzled. Burnout 3 draws its logo and frontend
     * header art this way; without a case every such quad came out as the
     * unsupported-format fill, a white box where the logo should be. */
    case 0x0B:                                      /* SZ_I8_A8R8G8B8 */
        if (!t->palette)
            return 0;
        *argb = ((const uint32_t *)(mem + t->palette))[p[u]];
        return 1;

    /* 32-bit, other channel orders. The name gives the byte order from the
     * top of the dword down, so each is a permutation of the same four. */
    case 0x3F: {                                    /* LIN_A8B8G8R8 */
        uint32_t t = ((const uint32_t *)p)[u];
        *argb = (t & 0xFF00FF00u) | ((t & 0xFF) << 16) | ((t >> 16) & 0xFF);
        return 1;
    }
    case 0x40: {                                    /* LIN_B8G8R8A8 */
        uint32_t t = ((const uint32_t *)p)[u];
        *argb = ((t & 0xFFu) << 24)                 /* A, from the bottom */
              | (((t >>  8) & 0xFFu) << 16)         /* R */
              | (((t >> 16) & 0xFFu) <<  8)         /* G */
              |  ((t >> 24) & 0xFFu);               /* B, from the top */
        return 1;
    }
    case 0x41: {                                    /* LIN_R8G8B8A8 */
        uint32_t t = ((const uint32_t *)p)[u];
        *argb = ((t & 0xFFu) << 24) | (t >> 8);
        return 1;
    }

    /* 16-bit. */
    case 0x10: {                                    /* LIN_A1R5G5B5 */
        uint32_t t = ((const uint16_t *)p)[u];
        *argb = ((t & 0x8000u) ? 0xFF000000u : 0u)
              | (expand((t >> 10) & 0x1F, 5) << 16)
              | (expand((t >>  5) & 0x1F, 5) <<  8)
              |  expand( t        & 0x1F, 5);
        return 1;
    }
    case 0x1C: {                                    /* LIN_X1R5G5B5 */
        uint32_t t = ((const uint16_t *)p)[u];
        *argb = 0xFF000000u
              | (expand((t >> 10) & 0x1F, 5) << 16)
              | (expand((t >>  5) & 0x1F, 5) <<  8)
              |  expand( t        & 0x1F, 5);
        return 1;
    }
    case 0x11: {                                    /* LIN_R5G6B5 */
        uint32_t t = ((const uint16_t *)p)[u];
        *argb = 0xFF000000u
              | (expand((t >> 11) & 0x1F, 5) << 16)
              | (expand((t >>  5) & 0x3F, 6) <<  8)
              |  expand( t        & 0x1F, 5);
        return 1;
    }
    case 0x1D: {                                    /* LIN_A4R4G4B4 */
        uint32_t t = ((const uint16_t *)p)[u];
        *argb = (expand((t >> 12) & 0x0F, 4) << 24)
              | (expand((t >>  8) & 0x0F, 4) << 16)
              | (expand((t >>  4) & 0x0F, 4) <<  8)
              |  expand( t        & 0x0F, 4);
        return 1;
    }

    /* 8-bit. */
    case 0x13: {                                    /* LIN_L8 */
        uint32_t t = p[u];
        *argb = 0xFF000000u | (t << 16) | (t << 8) | t;
        return 1;
    }
    case 0x1F:                                      /* LIN_A8 */
        *argb = ((uint32_t)p[u] << 24) | 0x00FFFFFFu;
        return 1;

    /* 4:2:2 packed YUV, two texels per four bytes.
     *
     * This is how a title hands over a decoded video frame, and without it
     * the frame falls through to `default` -- which returns 0, so the caller
     * paints the quad's vertex colour and the movie is a flat rectangle.
     *
     * The chroma pair is shared between an even texel and the one after it,
     * so the group is found by masking the bottom bit of the index. BT.601,
     * the same coefficients the D3D8 upload path converts with, so the two
     * paths agree rather than each having its own idea of the colour. */
    case 0x24:                                      /* LC_CR8YB8CB8YA8, YUY2 */
    case 0x25: {                                    /* LC_YB8CR8YA8CB8, UYVY */
        uint32_t yoff = (fmt == 0x24) ? 0u : 1u;
        const uint8_t *g = p + (size_t)(u & ~1u) * 2;
        int c  = (int)g[(u & 1u) ? 2 + yoff : yoff] - 16;
        int cu = (int)g[1 - yoff] - 128;
        int cv = (int)g[3 - yoff] - 128;
        int r = (298 * c + 409 * cv + 128) >> 8;
        int gg = (298 * c - 100 * cu - 208 * cv + 128) >> 8;
        int b = (298 * c + 516 * cu + 128) >> 8;
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (gg < 0) gg = 0;
        if (gg > 255) gg = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        *argb = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8)
              | (uint32_t)b;
        return 1;
    }

    default:
        return 0;
    }
}

static int sample_texture(uint32_t u, uint32_t v, uint32_t *argb)
{
    return sample_tex(&s_gpu.texs[0], 0, u, v, argb);
}

/* Write a bound texture out as a BMP, through the sampler rather than around it.
 *
 * "Which texture is this" is not answerable from an address and a format, and
 * it is the question behind most of the ones that matter -- is that a font
 * page or an icon atlas, did the swizzle decode, is the alpha inverted. Going
 * through sample_texture means the file shows exactly what the rasteriser
 * sees, so a decode bug appears here rather than only as a wrong-looking
 * triangle.
 *
 * ponytail: RGB only, alpha dropped. A glyph page is alpha and would come out
 * black, so alpha is composited onto mid-grey to stay legible; that is a
 * viewing choice, not a decode. One file per distinct texture, first use only.
 */
static void dump_texture_bmp(uint32_t seq)
{
    const char *prefix = getenv("RECOMP_TEX_DUMP");
    uint32_t w = s_gpu.texs[0].width, h = s_gpu.texs[0].height, x, y;
    uint32_t row_bytes, pad, filesz;
    uint8_t hdr[54];
    char path[512];
    FILE *f;

    if (!prefix || !w || !h || w > 4096 || h > 4096)
        return;
    row_bytes = w * 3;
    pad = (4 - (row_bytes & 3)) & 3;
    filesz = 54 + (row_bytes + pad) * h;

    snprintf(path, sizeof path, "%s%02u_%08X_fmt%02X.bmp",
             prefix, seq, s_gpu.texs[0].offset, s_gpu.texs[0].color);
    f = fopen(path, "wb");
    if (!f)
        return;
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &filesz, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, sizeof hdr, f);

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint32_t argb = 0, a;
            uint8_t px[3];
            if (!sample_texture(x, h - 1 - y, &argb))
                argb = 0;
            a = (argb >> 24) & 0xFFu;
            /* over mid-grey, so an alpha-only page is visible either way */
            px[0] = (uint8_t)(((argb & 0xFFu) * a + 128u * (255u - a)) / 255u);
            px[1] = (uint8_t)((((argb >> 8) & 0xFFu) * a + 128u * (255u - a)) / 255u);
            px[2] = (uint8_t)((((argb >> 16) & 0xFFu) * a + 128u * (255u - a)) / 255u);
            fwrite(px, 1, 3, f);
        }
        if (pad) {
            static const uint8_t zero[3] = {0, 0, 0};
            fwrite(zero, 1, pad, f);
        }
    }
    fclose(f);
    fprintf(stderr, "  [TEXDUMP] %s (%ux%u fmt 0x%02X)\n",
            path, w, h, s_gpu.texs[0].color);
    fflush(stderr);
}

/* The surface, resolved once per batch.
 *
 * dma_resolve consults the contiguous arena's high-water mark and
 * surface_hits_image walks the image range; both were being done per pixel
 * -- dma_resolve twice -- which cost more than the rasterisation they
 * guarded. Neither answer can change inside a batch, because the colour
 * offset arrives as a method and a method cannot arrive mid-triangle.
 *
 * This is not a micro-optimisation for its own sake: the loader's video
 * paces on frames actually presented, so the rasteriser's throughput is the
 * playback rate. */
static uint8_t *s_surface;          /* host address of surface row 0 */

static int surface_begin_batch(const uint8_t *mem)
{
    uint32_t base = dma_resolve(s_gpu.color_offset);

    if (surface_hits_image(base, (s_gpu.clip_y + s_gpu.clip_h) * s_gpu.pitch))
        return 0;
    s_surface = (uint8_t *)mem + base;
    return 1;
}

static uint32_t pack_color(const float c[4]);

/* One GL blend factor, per channel (r g b a). */
static void blend_factor(uint32_t f, const float s[4], const float d[4],
                         float out[4])
{
    int k;
    for (k = 0; k < 4; k++) {
        float c = (float)((s_gpu.blend_color >> (k == 3 ? 24 : 16 - 8 * k))
                          & 0xFF) / 255.0f;
        switch (f) {
        case 0x0000: out[k] = 0.0f;            break;   /* ZERO                */
        case 0x0001: out[k] = 1.0f;            break;   /* ONE                 */
        case 0x0300: out[k] = s[k];            break;   /* SRC_COLOR           */
        case 0x0301: out[k] = 1.0f - s[k];     break;
        case 0x0302: out[k] = s[3];            break;   /* SRC_ALPHA           */
        case 0x0303: out[k] = 1.0f - s[3];     break;
        case 0x0304: out[k] = d[3];            break;   /* DST_ALPHA           */
        case 0x0305: out[k] = 1.0f - d[3];     break;
        case 0x0306: out[k] = d[k];            break;   /* DST_COLOR           */
        case 0x0307: out[k] = 1.0f - d[k];     break;
        case 0x0308: out[k] = k == 3 ? 1.0f : fminf(s[3], 1.0f - d[3]); break;
        case 0x8001: out[k] = c;               break;   /* CONSTANT_COLOR      */
        case 0x8002: out[k] = 1.0f - c;        break;
        case 0x8003: out[k] = (float)(s_gpu.blend_color >> 24) / 255.0f; break;
        case 0x8004: out[k] = 1.0f - (float)(s_gpu.blend_color >> 24) / 255.0f; break;
        default:     out[k] = f ? 1.0f : 0.0f; break;
        }
    }
}

static void put_pixel(uint8_t *mem, uint32_t bpp, int x, int y, uint32_t argb)
{
    uint8_t *row;

    (void)mem;
    if (x < (int)s_gpu.clip_x || x >= (int)(s_gpu.clip_x + s_gpu.clip_w))
        return;
    if (y < (int)s_gpu.clip_y || y >= (int)(s_gpu.clip_y + s_gpu.clip_h))
        return;
    s_gpu.pixels++;
    if ((argb & 0x00FFFFFFu) > (s_gpu.pixel_max & 0x00FFFFFFu))
        s_gpu.pixel_max = argb;
    row = s_surface + (size_t)y * s_gpu.pitch;

    /* Blending, with the GL factor set and equations the NV2A takes.
     *
     * Only SRC_ALPHA / ONE_MINUS_SRC_ALPHA used to be honoured and every other
     * pair was written opaque. Menus never noticed; a race does: Burnout 3
     * finishes its 3D frame with screen-space passes that modulate or add
     * over the scene, and written opaque they paint the whole view over. */
    if (s_gpu.blend_enable
        && !(s_gpu.blend_sfactor == 1 && s_gpu.blend_dfactor == 0)) {
        uint32_t dst = 0xFF000000u;
        float sc[4], dc[4], sf[4], df[4], out[4];
        int k;
        if (bpp == 4) {
            dst = ((const uint32_t *)row)[x];
        } else if (bpp == 2) {
            uint32_t t = ((const uint16_t *)row)[x];
            dst = 0xFF000000u | ((t & 0xF800u) << 8) | ((t & 0x07E0u) << 5)
                | ((t & 0x001Fu) << 3);
        }
        for (k = 0; k < 4; k++) {           /* r g b a, 0..1 */
            int sh = k == 3 ? 24 : 16 - 8 * k;
            sc[k] = (float)((argb >> sh) & 0xFF) / 255.0f;
            dc[k] = (float)((dst  >> sh) & 0xFF) / 255.0f;
        }
        blend_factor(s_gpu.blend_sfactor, sc, dc, sf);
        blend_factor(s_gpu.blend_dfactor, sc, dc, df);
        for (k = 0; k < 4; k++) {
            float a = sc[k] * sf[k], b = dc[k] * df[k];
            switch (s_gpu.blend_equation) {
            case 0x800A: out[k] = a - b; break;               /* SUBTRACT     */
            case 0x800B: out[k] = b - a; break;               /* REV_SUBTRACT */
            case 0x8007: out[k] = fminf(sc[k], dc[k]); break; /* MIN          */
            case 0x8008: out[k] = fmaxf(sc[k], dc[k]); break; /* MAX          */
            default:     out[k] = a + b; break;               /* ADD          */
            }
        }
        argb = pack_color(out);
    }

    if (s_gpu.color_mask != 0x01010101u) {
        uint32_t keep = 0, old;
        if (!(s_gpu.color_mask & 0x01000000u)) keep |= 0xFF000000u;
        if (!(s_gpu.color_mask & 0x00010000u)) keep |= 0x00FF0000u;
        if (!(s_gpu.color_mask & 0x00000100u)) keep |= 0x0000FF00u;
        if (!(s_gpu.color_mask & 0x00000001u)) keep |= 0x000000FFu;
        if (keep == 0xFFFFFFFFu)
            return;
        if (bpp == 4) {
            old = ((const uint32_t *)row)[x];
        } else {
            uint32_t t = ((const uint16_t *)row)[x];
            old = ((t & 0xF800u) << 8) | ((t & 0x07E0u) << 5) | ((t & 0x001Fu) << 3);
        }
        argb = (argb & ~keep) | (old & keep);
    }
    if (bpp == 4) {
        ((uint32_t *)row)[x] = argb;
    } else if (bpp == 2) {
        ((uint16_t *)row)[x] = (uint16_t)(((argb >> 8) & 0xF800)
                                        | ((argb >> 5) & 0x07E0)
                                        | ((argb >> 3) & 0x001F));
    }
}

/* Half-space fill. Barycentric edge functions rather than scanline slopes:
 * the same test decides both windings, so a title that emits clockwise
 * triangles does not silently render nothing. */
static void raster_triangle(const float a[2], const float b[2],
                            const float c[2], uint32_t argb,
                            const float uv[3][2])
{
    uint8_t *mem = (uint8_t *)xbox_GetMemoryOffset();
    uint32_t bpp = surface_bpp();
    float area;
    int minx, maxx, miny, maxy, x, y;
    int textured = uv && s_gpu.texs[0].valid;

    if (bpp != 4 && bpp != 2)
        return;

    area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (area == 0.0f)
        return;                            /* degenerate */

    /* Where this batch writes. The same check the per-pixel path made, made
     * once: a surface address landing on the title's own image is no safer
     * one pixel at a time than 4.9 MB at once. */
    if (!surface_begin_batch(mem))
        return;

    minx = (int)floorf(fminf(a[0], fminf(b[0], c[0])));
    maxx = (int)ceilf (fmaxf(a[0], fmaxf(b[0], c[0])));
    miny = (int)floorf(fminf(a[1], fminf(b[1], c[1])));
    maxy = (int)ceilf (fmaxf(a[1], fmaxf(b[1], c[1])));

    if (minx < (int)s_gpu.clip_x) minx = (int)s_gpu.clip_x;
    if (miny < (int)s_gpu.clip_y) miny = (int)s_gpu.clip_y;
    if (maxx > (int)(s_gpu.clip_x + s_gpu.clip_w)) maxx = (int)(s_gpu.clip_x + s_gpu.clip_w);
    if (maxy > (int)(s_gpu.clip_y + s_gpu.clip_h)) maxy = (int)(s_gpu.clip_y + s_gpu.clip_h);
    if (minx >= maxx || miny >= maxy) {
        s_gpu.tris_skipped_offscreen++;
        return;
    }

    for (y = miny; y < maxy; y++) {
        for (x = minx; x < maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
            float w1 = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
            float w2 = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
            if (!((w0 >= 0 && w1 >= 0 && w2 >= 0)
               || (w0 <= 0 && w1 <= 0 && w2 <= 0)))
                continue;
            if (textured) {
                /* Barycentric, straight from the edge functions already
                 * computed: w1 is the area opposite a, w2 opposite b, w0
                 * opposite c, and the three sum to the whole triangle.
                 *
                 * No perspective divide. These are screen-space vertices with
                 * no w to divide by -- which is exactly the case a full-screen
                 * pass is, and the only case that reaches here. */
                uint32_t texel;
                float su = (w1 * uv[0][0] + w2 * uv[1][0] + w0 * uv[2][0]) / area;
                float sv = (w1 * uv[0][1] + w2 * uv[1][1] + w0 * uv[2][1]) / area;
                if (su < 0.0f) su = 0.0f;
                if (sv < 0.0f) sv = 0.0f;
                if (sample_texture((uint32_t)su, (uint32_t)sv, &texel)) {
                    put_pixel(mem, bpp, x, y, texel);
                    continue;
                }
            }
            put_pixel(mem, bpp, x, y, argb);
        }
    }
    s_gpu.tris_drawn++;
    note_drawn();
}

/* Attribute 3 is diffuse colour in every NV2A layout that sets one. Absent it,
 * white -- a visible wrong colour beats an invisible correct one during
 * bring-up. */
/* Which attribute carries the colour.
 *
 * Slot 3 is diffuse by convention and titles that follow it are read straight
 * from there. Half-Life 2 does not: its vertex is position, colour, texcoord
 * at stride 24, and the colour arrives in slot 5. So fall back to the format
 * rather than the slot number -- D3DCOLOR is the one attribute type that is
 * only ever a colour, which makes it a stronger signal than the convention. */
static const VertexAttr *color_attr(void)
{
    uint32_t a;

    if (s_gpu.attr[3].offset && s_gpu.attr[3].stride)
        return &s_gpu.attr[3];
    for (a = 0; a < NV_VERTEX_ATTRS; a++)
        if (s_gpu.attr[a].type == 0 && s_gpu.attr[a].size == 4
                && s_gpu.attr[a].offset && s_gpu.attr[a].stride)
            return &s_gpu.attr[a];
    return &s_gpu.attr[3];
}

/* Attribute 9 is texture coordinate 0 in the NV2A vertex layout, the same way
 * 0 is position and 3 is diffuse -- for a title that follows the convention.
 *
 * Half-Life 2 does not, in either place. Its menu and HUD vertex is position,
 * colour, texcoord at stride 24, with the colour in slot 5 and the texcoords
 * in slot 7, so reading slot 9 found nothing and every batch drew untextured.
 * That is invisible rather than wrong-looking: the menu paints a full-screen
 * quad and then draws its text over it, and with no sampling both come out
 * white, so the screen is blank white and nothing suggests the text was ever
 * drawn.
 *
 * Falling back to the format works because the three attributes of such a
 * vertex are distinguishable: position is float3, colour is D3DCOLOR, and a
 * float2 is a texture coordinate and nothing else.
 *
 * ponytail: takes the first float2 it finds, so a title with two texcoord sets
 * gets stage 0's -- which is what this single-texture rasteriser samples
 * anyway. Multi-texture wants the D3D11 translator, not another heuristic. */
static const VertexAttr *texcoord_attr(void)
{
    uint32_t a;

    if (s_gpu.attr[9].offset && s_gpu.attr[9].stride)
        return &s_gpu.attr[9];
    for (a = 0; a < NV_VERTEX_ATTRS; a++)
        if (s_gpu.attr[a].type == 2 && s_gpu.attr[a].size == 2
                && s_gpu.attr[a].offset && s_gpu.attr[a].stride)
            return &s_gpu.attr[a];
    return &s_gpu.attr[9];
}

/* Texel coordinates, whichever convention the title used.
 *
 * The two are not interchangeable and the format decides which is in force: a
 * swizzled texture is addressed in [0,1], a linear one in texels. Both are
 * scaled to texels here so that everything downstream -- the barycentric
 * interpolation and the sampler -- works in one unit.
 *
 * This mattered the moment swizzled formats became samplable. Normalised
 * coordinates truncated to a texel index land on texel 0 for any coordinate
 * below 1.0, so a whole quad sampled a single texel and came out flat: the
 * background painted one near-black colour, which looks like a texture that
 * decoded wrong rather than one that was never indexed. */
static int fetch_texcoord(uint32_t index, float out[2])
{
    float t[4];

    if (!fetch_attr(texcoord_attr(), index, t))
        return 0;
    out[0] = t[0];
    out[1] = t[1];
    if (tex_size_from_format(s_gpu.texs[0].color)) {
        out[0] *= (float)s_gpu.texs[0].width;
        out[1] *= (float)s_gpu.texs[0].height;
    }
    return 1;
}

static uint32_t vertex_color(uint32_t index)
{
    float c[4];

    if (!fetch_attr(color_attr(), index, c))
        return 0xFFFFFFFFu;
    return ((uint32_t)(c[3] * 255.0f) << 24)
         | ((uint32_t)(c[0] * 255.0f) << 16)
         | ((uint32_t)(c[1] * 255.0f) <<  8)
         |  (uint32_t)(c[2] * 255.0f);
}

/* An untransformed batch drawn as if it were screen space smears a few pixels
 * into the corner, so the batch has to be classified before it is rasterised.
 *
 * This used to demand that every vertex land inside the surface, which is a
 * different question and the wrong one: geometry that extends past the
 * viewport is ordinary, and clipping it is raster_triangle's job (it clamps
 * its span to the clip rect). The dashboard is exactly the case that exposed
 * it -- a full-screen pass drawn as one oversized triangle, vertices at
 * (-0.5,-0.5), (2*w,-0.5), (-0.5,2*h), all correct and all rejected.
 *
 * What actually separates the two is scale. Object-space positions are model
 * units, a handful either side of the origin; screen-space ones are measured
 * in pixels of a surface hundreds of pixels wide. So: the batch has to be able
 * to touch the surface at all, and it has to be bigger than object space.
 *
 * ponytail: a genuinely tiny screen-space sprite reads as object space and is
 * skipped. It is counted as skipped rather than silently dropped, and the
 * unambiguous answer needs the vertex-program state, which is not tracked yet.
 */
#define OBJECT_SPACE_SPAN 8.0f

static int batch_is_screen_space(void)
{
    float p[4], lo_x, hi_x, lo_y, hi_y;
    uint32_t i;

    if (!s_gpu.clip_w || !s_gpu.clip_h || !s_gpu.idx_count)
        return 0;
    if (!fetch_attr(&s_gpu.attr[0], s_gpu.idx[0], p))
        return 0;
    lo_x = hi_x = p[0];
    lo_y = hi_y = p[1];
    for (i = 1; i < s_gpu.idx_count; i++) {
        if (!fetch_attr(&s_gpu.attr[0], s_gpu.idx[i], p))
            return 0;
        if (p[0] < lo_x) lo_x = p[0];
        if (p[0] > hi_x) hi_x = p[0];
        if (p[1] < lo_y) lo_y = p[1];
        if (p[1] > hi_y) hi_y = p[1];
    }

    /* Entirely off the surface: nothing to draw under either reading. */
    if (hi_x < (float)s_gpu.clip_x
     || lo_x > (float)(s_gpu.clip_x + s_gpu.clip_w)
     || hi_y < (float)s_gpu.clip_y
     || lo_y > (float)(s_gpu.clip_y + s_gpu.clip_h))
        return 0;

    /* Small enough to be model units rather than pixels. */
    if (hi_x - lo_x < OBJECT_SPACE_SPAN && hi_y - lo_y < OBJECT_SPACE_SPAN)
        return 0;

    return 1;
}

/* NV097 primitive types.
 *
 * These are the operand of SET_BEGIN_END, where 0 is END and the list starts
 * at 1. They were each one too low, so every title's geometry was decomposed
 * as the primitive below the one it asked for -- a strip as a fan, a fan as
 * quads, and TRIANGLES, the one case whose vertex count must be a multiple
 * of three, as a strip.
 *
 * The vertex order says which numbering is right without taking a table on
 * trust: a strip arrives in Z order and a fan in cyclic order, and they only
 * line up with the primitive under this one. */
#define NV_PRIM_POINTS         1
#define NV_PRIM_LINES          2
#define NV_PRIM_LINE_LOOP      3
#define NV_PRIM_LINE_STRIP     4
#define NV_PRIM_TRIANGLES      5
#define NV_PRIM_TRIANGLE_STRIP 6
#define NV_PRIM_TRIANGLE_FAN   7
#define NV_PRIM_QUADS          8
#define NV_PRIM_QUAD_STRIP     9
#define NV_PRIM_POLYGON        10

/* How many post-draw captures to keep: enough to see whether the geometry
 * is stable from frame to frame, few enough not to fill a directory. */
#define FB_DUMP_AFTER_DRAW 8
static int s_drawn_dumps;

static void dump_surface_bmp(void);

/* One triangle by vertex index: gather position and, if the batch has one,
 * texture coordinate 0. A vertex whose position cannot be read is not drawn;
 * a batch whose texcoords cannot be read is drawn untextured rather than not
 * at all, so a missing coordinate stream costs the colour and not the shape.
 */
static void raster_indexed(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t argb)
{
    float p[3][4], uv[3][2];
    int textured;

    if (!fetch_attr(&s_gpu.attr[0], i0, p[0])
     || !fetch_attr(&s_gpu.attr[0], i1, p[1])
     || !fetch_attr(&s_gpu.attr[0], i2, p[2]))
        return;

    textured = fetch_texcoord(i0, uv[0])
            && fetch_texcoord(i1, uv[1])
            && fetch_texcoord(i2, uv[2]);

    raster_triangle(p[0], p[1], p[2], argb,
                    textured ? (const float (*)[2])uv : NULL);
}

/* ---- 3D: vertex programs and depth -------------------------------------
 *
 * A batch drawn while TRANSFORM_EXECUTION_MODE says "program" carries
 * model-space positions. Each vertex is run through the title's own vertex
 * program (nv2a_vsh_interp.c), whose oPos output on Xbox D3D is already
 * screen space -- the runtime appends the viewport transform and the divide
 * by w -- with the clip-space w left in oPos.w. So the rasteriser gets pixel
 * coordinates and a depth, and w for perspective-correct texturing.
 *
 * ponytail: no clipping. A triangle with any vertex behind the eye (w <= 0)
 * is dropped rather than clipped, which loses the slivers that cross the near
 * plane; road right under the camera is where that shows. Clip in
 * homogeneous space if it matters. */

/* Depth buffers, host-side, one per zeta surface the title uses. The real one
 * lives in guest memory in a tiled format nothing here decodes, and nothing
 * the title does reads it back, so a float buffer per zeta offset is enough. */
#define NV_ZBUF_W 1024
#define NV_ZBUF_H 1024
static struct { uint32_t offset; float *z; } s_zbufs[4];
static int s_ftrace;            /* frame trace: 0 idle, 2 tracing, 3 done */
static int s_zbuf_next;

static float zclear_value(void)
{
    /* Zeta format in SURFACE_FORMAT bits 4-7: 1 is Z16, 2 is Z24S8. */
    uint32_t zf = (s_gpu.format >> 4) & 0xF;
    return zf == 1 ? (float)(s_gpu.zstencil_clear & 0xFFFF)
                   : (float)(s_gpu.zstencil_clear >> 8);
}

static float *zbuf_current(int create)
{
    int i;
    size_t k;

    for (i = 0; i < 4; i++)
        if (s_zbufs[i].z && s_zbufs[i].offset == s_gpu.zeta_offset)
            return s_zbufs[i].z;
    if (!create)
        return NULL;
    i = s_zbuf_next++ & 3;
    if (!s_zbufs[i].z)
        s_zbufs[i].z = (float *)malloc(sizeof(float) * NV_ZBUF_W * NV_ZBUF_H);
    if (!s_zbufs[i].z)
        return NULL;
    s_zbufs[i].offset = s_gpu.zeta_offset;
    for (k = 0; k < (size_t)NV_ZBUF_W * NV_ZBUF_H; k++)
        s_zbufs[i].z[k] = 3.4e38f;
    return s_zbufs[i].z;
}

static void zbuf_clear(void)
{
    float *z = zbuf_current(1), v = zclear_value();
    size_t k;
    if (z)
        for (k = 0; k < (size_t)NV_ZBUF_W * NV_ZBUF_H; k++)
            z[k] = v;
}

static int depth_pass(float z, float stored)
{
    switch (s_gpu.depth_func) {                   /* GL enums, as the NV2A */
    case 0x200: return 0;
    case 0x201: return z <  stored;
    case 0x202: return z == stored;
    case 0x203: return z <= stored;
    case 0x204: return z >  stored;
    case 0x205: return z != stored;
    case 0x206: return z >= stored;
    default:    return 1;
    }
}

static uint32_t pack_color(const float c[4])
{
    int i;
    uint32_t b[4];
    for (i = 0; i < 4; i++) {
        float f = c[i] < 0.0f ? 0.0f : (c[i] > 1.0f ? 1.0f : c[i]);
        b[i] = (uint32_t)(f * 255.0f + 0.5f);
    }
    return (b[3] << 24) | (b[0] << 16) | (b[1] << 8) | b[2];
}

static uint32_t modulate(uint32_t t, const float c[4])
{
    float f[4];
    f[0] = (float)((t >> 16) & 0xFF) / 255.0f * c[0];
    f[1] = (float)((t >> 8) & 0xFF) / 255.0f * c[1];
    f[2] = (float)(t & 0xFF) / 255.0f * c[2];
    f[3] = (float)(t >> 24) / 255.0f * c[3];
    return pack_color(f);
}

/* Bytes one texel of a (non-DXT) format takes, for cube-face strides. */
static uint32_t tex_texel_bytes(uint32_t fmt)
{
    switch (fmt) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1F:
        return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11:
    case 0x1C: case 0x1D:
        return 2;
    default:
        return 4;
    }
}

/* Bytes between cube faces: one face with all its mip levels, rounded up to
 * the NV2A's 128-byte face alignment (xemu texture.c). */
static uint32_t tex_face_stride(const Texture *t)
{
    uint32_t w = t->width, h = t->height, lv, total = 0;
    uint32_t block = d3d8_format_dxt_block_bytes(t->color);
    uint32_t levels = t->levels ? t->levels : 1;

    if (!tex_size_from_format(t->color))
        return (t->pitch * t->height + 127u) & ~127u;
    for (lv = 0; lv < levels; lv++) {
        uint32_t lw = (w >> lv) ? (w >> lv) : 1, lh = (h >> lv) ? (h >> lv) : 1;
        total += block ? ((lw + 3) / 4) * ((lh + 3) / 4) * block
                       : lw * lh * tex_texel_bytes(t->color);
    }
    return (total + 127u) & ~127u;
}

/* One texel at normalised (swizzled/DXT) or texel (linear) coordinates, as
 * floats. An unusable stage reads white, so a missing texture multiplies
 * through instead of blacking the pixel out. */
static void rc_texel(const Texture *t, uint32_t face, float u, float v,
                     float out[4])
{
    uint32_t texel, mag = (t->filter >> 24) & 0xF, min = (t->filter >> 16) & 0xFF;
    if (tex_size_from_format(t->color)) {
        u *= (float)t->width;
        v *= (float)t->height;
    }
    /* TENT (bilinear) when the title asks for it, magnifying or minifying:
     * a 64x32 sky gradient stretched over the screen is blocks with nearest
     * texels and a gradient with four. MAG 2 is tent; MIN 2 is tent at LOD 0
     * and 4/6 the tent mip modes.
     * ponytail: level 0 only, no mip selection -- minified textures shimmer.
     * Add LOD from the screen-space derivative when that shows. */
    if (mag == 2 || min == 2 || min == 4 || min == 6) {
        float fu = u - 0.5f, fv = v - 0.5f, wu, wv, c[4][4];
        int32_t iu = (int32_t)floorf(fu), iv = (int32_t)floorf(fv), k, j;
        wu = fu - (float)iu;
        wv = fv - (float)iv;
        for (k = 0; k < 4; k++) {
            if (sample_tex(t, face, (uint32_t)(iu + (k & 1)),
                           (uint32_t)(iv + (k >> 1)), &texel))
                nv2a_rc_unpack(texel, c[k]);
            else
                c[k][0] = c[k][1] = c[k][2] = c[k][3] = 1.0f;
        }
        for (j = 0; j < 4; j++)
            out[j] = (c[0][j] * (1.0f - wu) + c[1][j] * wu) * (1.0f - wv)
                   + (c[2][j] * (1.0f - wu) + c[3][j] * wu) * wv;
        return;
    }
    if (sample_tex(t, face, (uint32_t)(int32_t)floorf(u),
                   (uint32_t)(int32_t)floorf(v), &texel))
        nv2a_rc_unpack(texel, out);
    else
        out[0] = out[1] = out[2] = out[3] = 1.0f;
}

/* What texture stage `st` contributes, by its SHADER_STAGE_PROGRAM mode
 * (xemu psh.c). Returns 0 when a clip-plane stage kills the pixel. */
static int rc_stage_fetch(int st, const float c[4], float out[4])
{
    const Texture *t = &s_gpu.texs[st];
    uint32_t mode = (s_gpu.rc.stage_program >> (st * 5)) & 0x1F, j;
    float q = c[3] != 0.0f ? c[3] : 1.0f;

    switch (mode) {
    case 0:                                       /* NONE */
        out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
        return 1;
    case 1: case 2:                               /* PROJECT2D / 3D */
        rc_texel(t, 0, c[0] / q, c[1] / q, out);
        return 1;
    case 3: {                                     /* CUBEMAP */
        float x = c[0], y = c[1], z = c[2];
        float ax = fabsf(x), ay = fabsf(y), az = fabsf(z), ma, sc, tc;
        uint32_t face;
        if (!t->cube) {
            rc_texel(t, 0, c[0], c[1], out);
            return 1;
        }
        if (ax >= ay && ax >= az) {
            face = x > 0 ? 0 : 1; ma = ax; sc = x > 0 ? -z : z; tc = -y;
        } else if (ay >= az) {
            face = y > 0 ? 2 : 3; ma = ay; sc = x; tc = y > 0 ? z : -z;
        } else {
            face = z > 0 ? 4 : 5; ma = az; sc = z > 0 ? x : -x; tc = -y;
        }
        if (ma == 0.0f)
            ma = 1.0f;
        rc_texel(t, face * tex_face_stride(t), (sc / ma + 1.0f) * 0.5f,
                 (tc / ma + 1.0f) * 0.5f, out);
        return 1;
    }
    case 4:                                       /* PASSTHRU */
        for (j = 0; j < 4; j++)
            out[j] = c[j] < 0.0f ? 0.0f : (c[j] > 1.0f ? 1.0f : c[j]);
        return 1;
    case 5:                                       /* CLIPPLANE */
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        for (j = 0; j < 4; j++) {
            int ge = (s_gpu.clip_plane_mode >> (st * 4 + j)) & 1;
            if (ge ? c[j] >= 0.0f : c[j] < 0.0f)
                return 0;
        }
        return 1;
    default:
        /* ponytail: bump-env and dot-product modes read zero; they are water
         * and bump effects -- add them when a title's look depends on one. */
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return 1;
    }
}

/* The fixed-function fog unit, per vertex, from the program's oFog.x
 * (xemu vsh.c). Fog disabled reads as factor 1: no fog. */
static float fog_factor(float d)
{
    float f, px = s_gpu.fog_param[0], py = s_gpu.fog_param[1];
    if (!s_gpu.fog_enable)
        return 1.0f;
    switch (s_gpu.fog_mode) {
    case 0x800: case 0x802:                       /* EXP, EXP_ABS */
        f = px + exp2f(d * py * 16.0f) - 1.5f;
        break;
    case 0x801: case 0x803:                       /* EXP2, EXP2_ABS */
        f = px + exp2f(-d * d * py * py * 32.0f) - 1.5f;
        break;
    default:                                      /* LINEAR, LINEAR_ABS */
        f = px + d * py - 1.0f;
        break;
    }
    if (s_gpu.fog_mode == 0x802 || s_gpu.fog_mode == 0x803
        || s_gpu.fog_mode == 0x804)
        f = fabsf(f);
    if (f != f)
        f = 1.0f;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

static int alpha_test_pass(float a)
{
    int v = (int)(a * 255.0f + 0.5f), r = (int)(s_gpu.alpha_ref & 0xFF);
    switch (s_gpu.alpha_func) {
    case 0x200: return 0;                         /* NEVER    */
    case 0x201: return v <  r;                    /* LESS     */
    case 0x202: return v == r;                    /* EQUAL    */
    case 0x203: return v <= r;                    /* LEQUAL   */
    case 0x204: return v >  r;                    /* GREATER  */
    case 0x205: return v != r;                    /* NOTEQUAL */
    case 0x206: return v >= r;                    /* GEQUAL   */
    default:    return 1;                         /* ALWAYS   */
    }
}

/* Everything a triangle's pixels need, computed once per triangle, so the
 * pixel loop can run over any subset of rows on any thread. */
typedef struct {
    uint8_t *mem;
    uint32_t bpp;
    const Nv2aVshOutput *va, *vb, *vc;
    const float *a, *b, *c;
    float iw[3], uv[3][2], stc[3][4][4], vfog[3], fogc[4], inv_area;
    float *zb;
    int minx, maxx, miny, maxy, use_rc, textured;
} XfTri;

typedef struct { uint64_t depth_fail, pixels; uint32_t zpass; } XfCount;

/* Rows y0, y0+step, ... < maxy of triangle T. */
static void xf_rows(const XfTri *T, int y0, int step, XfCount *cnt)
{
    const float *a = T->a, *b = T->b, *c = T->c;
    const Nv2aVshOutput *va = T->va, *vb = T->vb, *vc = T->vc;
    int x, y, k;

    for (y = y0; y < T->maxy; y += step) {
        for (x = T->minx; x < T->maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
            float w1 = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
            float w2 = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
            float l0, l1, l2, z, col[4], pw;
            uint32_t argb;
            if (!((w0 >= 0 && w1 >= 0 && w2 >= 0)
               || (w0 <= 0 && w1 <= 0 && w2 <= 0)))
                continue;
            /* w1 is opposite a, w2 opposite b, w0 opposite c. */
            l0 = w1 * T->inv_area; l1 = w2 * T->inv_area; l2 = w0 * T->inv_area;
            z = l0 * a[2] + l1 * b[2] + l2 * c[2];
            {
                float *zp = T->zb ? &T->zb[(size_t)y * NV_ZBUF_W + x] : NULL;
                int shade = (s_gpu.color_mask & 0x01010101u) || s_gpu.alpha_test;
                /* Depth test, then shade, then alpha test, and only then the
                 * depth write: an alpha-tested texel that is cut away must
                 * not leave its depth behind (foliage, fences). */
                if (zp && s_gpu.depth_test && !depth_pass(z, *zp)) {
                    cnt->depth_fail++;
                    continue;
                }
                argb = 0;
                if (shade) {
                    for (k = 0; k < 4; k++)
                        col[k] = l0 * va->d0[k] + l1 * vb->d0[k] + l2 * vc->d0[k];
                    pw = l0 * T->iw[0] + l1 * T->iw[1] + l2 * T->iw[2];
                    if (T->use_rc) {
                        float d1[4], fog[4], t[4][4], out[4], tc[4];
                        int st, j, keep = 1;
                        for (k = 0; k < 4; k++)
                            d1[k] = l0 * va->d1[k] + l1 * vb->d1[k] + l2 * vc->d1[k];
                        fog[0] = T->fogc[0]; fog[1] = T->fogc[1]; fog[2] = T->fogc[2];
                        fog[3] = l0 * T->vfog[0] + l1 * T->vfog[1] + l2 * T->vfog[2];
                        for (st = 0; st < 4 && keep; st++) {
                            if (!((s_gpu.rc.stage_program >> (st * 5)) & 0x1F)) {
                                t[st][0] = t[st][1] = t[st][2] = 0.0f;
                                t[st][3] = 1.0f;
                                continue;
                            }
                            for (j = 0; j < 4; j++)
                                tc[j] = (l0 * T->stc[0][st][j] + l1 * T->stc[1][st][j]
                                       + l2 * T->stc[2][st][j]) / pw;
                            keep = rc_stage_fetch(st, tc, t[st]);
                        }
                        if (!keep)
                            continue;
                        nv2a_rc_eval(&s_gpu.rc, col, d1, fog,
                                     (const float (*)[4])t, out);
                        if (s_gpu.alpha_test && !alpha_test_pass(out[3]))
                            continue;
                        argb = pack_color(out);
                    } else {
                        argb = pack_color(col);
                        if (T->textured) {
                            uint32_t texel;
                            float tu, tv;
                            tu = (l0 * T->uv[0][0] + l1 * T->uv[1][0] + l2 * T->uv[2][0]) / pw;
                            tv = (l0 * T->uv[0][1] + l1 * T->uv[1][1] + l2 * T->uv[2][1]) / pw;
                            if (sample_texture((uint32_t)(int32_t)floorf(tu),
                                               (uint32_t)(int32_t)floorf(tv), &texel))
                                argb = modulate(texel, col);
                        }
                        if (s_gpu.alpha_test
                            && !alpha_test_pass((float)(argb >> 24) / 255.0f))
                            continue;
                    }
                }
                if (zp && s_gpu.depth_mask)
                    *zp = z;
                if (s_gpu.zpass_enable)
                    cnt->zpass++;
                if (!(s_gpu.color_mask & 0x01010101u))
                    continue;              /* colour writes off: depth only */
                cnt->pixels++;
            }
            put_pixel(T->mem, T->bpp, x, y, argb);
        }
    }
}

/* Worker threads for big triangles.
 *
 * The rasteriser is a software GPU, and a Burnout 3 frame is dominated by a
 * handful of full-screen passes -- composites, blurs, the menu backdrops --
 * each two triangles of 300,000 pixels through the register combiners. Those
 * split cleanly by row: every pixel reads only its own depth and colour, so
 * interleaved rows on N threads need no locking. Small triangles stay on the
 * executor thread, where waking workers would cost more than it saves.
 * RECOMP_RASTER_THREADS=<n> sets the count (1 = off); the default leaves a
 * few cores for the title and the host.
 * ponytail: s_gpu.pixels/pixel_max in put_pixel are unsynchronised stats and
 * may undercount; nothing depends on them. */
#define NV_RASTER_MAX_THREADS 16
#define NV_RASTER_MT_MIN_PIXELS 8192

#if defined(_WIN32)
static struct {
    int n;                                  /* threads incl. the caller */
    HANDLE start[NV_RASTER_MAX_THREADS], done;
    volatile LONG pending;
    const XfTri *tri;
    XfCount cnt[NV_RASTER_MAX_THREADS];
} s_pool;

static DWORD WINAPI raster_worker(LPVOID arg)
{
    int k = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(s_pool.start[k], INFINITE);
        memset(&s_pool.cnt[k], 0, sizeof s_pool.cnt[k]);
        xf_rows(s_pool.tri, s_pool.tri->miny + k, s_pool.n, &s_pool.cnt[k]);
        if (InterlockedDecrement(&s_pool.pending) == 0)
            SetEvent(s_pool.done);
    }
    return 0;
}

static int raster_pool_size(void)
{
    static int init;
    if (!init) {
        const char *e = getenv("RECOMP_RASTER_THREADS");
        SYSTEM_INFO si;
        int n, k;
        init = 1;
        GetSystemInfo(&si);
        n = e ? atoi(e) : (int)si.dwNumberOfProcessors - 4;
        if (n < 1) n = 1;
        if (n > NV_RASTER_MAX_THREADS) n = NV_RASTER_MAX_THREADS;
        s_pool.n = n;
        if (n > 1) {
            s_pool.done = CreateEventA(NULL, FALSE, FALSE, NULL);
            for (k = 0; k < n - 1; k++) {
                s_pool.start[k] = CreateEventA(NULL, FALSE, FALSE, NULL);
                CloseHandle(CreateThread(NULL, 0, raster_worker,
                                         (LPVOID)(intptr_t)k, 0, NULL));
            }
        }
    }
    return s_pool.n;
}

static void xf_rows_parallel(const XfTri *T, XfCount *total)
{
    int n = raster_pool_size(), k;
    long px = (long)(T->maxx - T->minx) * (T->maxy - T->miny);

    if (n <= 1 || px < NV_RASTER_MT_MIN_PIXELS || T->maxy - T->miny < n) {
        xf_rows(T, T->miny, 1, total);
        return;
    }
    s_pool.tri = T;
    s_pool.pending = n - 1;
    for (k = 0; k < n - 1; k++)
        SetEvent(s_pool.start[k]);
    {
        XfCount mine = {0, 0, 0};
        xf_rows(T, T->miny + (n - 1), n, &mine);
        WaitForSingleObject(s_pool.done, INFINITE);
        total->depth_fail += mine.depth_fail;
        total->pixels += mine.pixels;
        total->zpass += mine.zpass;
    }
    for (k = 0; k < n - 1; k++) {
        total->depth_fail += s_pool.cnt[k].depth_fail;
        total->pixels += s_pool.cnt[k].pixels;
        total->zpass += s_pool.cnt[k].zpass;
    }
}
#else
static void xf_rows_parallel(const XfTri *T, XfCount *total)
{
    xf_rows(T, T->miny, 1, total);
}
#endif

static void raster_xf_triangle(const Nv2aVshOutput *va, const Nv2aVshOutput *vb,
                               const Nv2aVshOutput *vc)
{
    XfTri T;
    XfCount cnt = {0, 0, 0};
    const Nv2aVshOutput *v[3];
    const float *a, *b, *c;
    float area, su = 1.0f, sv = 1.0f;
    int k;
    static int no_rc = -1;

    if (no_rc < 0)
        no_rc = getenv("RECOMP_NO_COMBINERS") != NULL;
    memset(&T, 0, sizeof T);
    T.mem = (uint8_t *)xbox_GetMemoryOffset();
    T.bpp = surface_bpp();
    T.textured = s_gpu.texs[0].valid;
    T.use_rc = s_gpu.rc_seen && !no_rc;
    T.va = va; T.vb = vb; T.vc = vc;

    v[0] = va; v[1] = vb; v[2] = vc;
    a = va->pos; b = vb->pos; c = vc->pos;
    T.a = a; T.b = b; T.c = c;
    if (T.bpp != 4 && T.bpp != 2)
        return;
    if (a[3] <= 0.0f || b[3] <= 0.0f || c[3] <= 0.0f) {
        s_gpu.tris_behind++;
        return;
    }
    area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (area == 0.0f || area != area) {
        s_gpu.xf_degenerate++;
        return;
    }
    for (k = 0; k < 3; k++) {
        if (!s_gpu.xf_seeded)
            s_gpu.xf_min[k] = s_gpu.xf_max[k] = a[k];
        if (a[k] < s_gpu.xf_min[k]) s_gpu.xf_min[k] = a[k];
        if (a[k] > s_gpu.xf_max[k]) s_gpu.xf_max[k] = a[k];
    }
    s_gpu.xf_seeded = 1;
    if (!surface_begin_batch(T.mem))
        return;
    if (s_gpu.depth_test || s_gpu.depth_mask)
        T.zb = zbuf_current(1);
    if (tex_size_from_format(s_gpu.texs[0].color)) {
        su = (float)s_gpu.texs[0].width;
        sv = (float)s_gpu.texs[0].height;
    }
    for (k = 0; k < 3; k++) {
        T.iw[k] = 1.0f / v[k]->pos[3];
        T.uv[k][0] = v[k]->tex[0][0] * su * T.iw[k];
        T.uv[k][1] = v[k]->tex[0][1] * sv * T.iw[k];
    }
    if (T.use_rc) {
        int st, j;
        for (k = 0; k < 3; k++) {
            for (st = 0; st < 4; st++)
                for (j = 0; j < 4; j++)
                    T.stc[k][st][j] = v[k]->tex[st][j] * T.iw[k];
            T.vfog[k] = fog_factor(v[k]->fog[0]);
        }
        /* FOG_COLOR is R in bits 0-7, the reverse of a D3DCOLOR. */
        nv2a_rc_unpack(s_gpu.fog_color, T.fogc);
        { float r = T.fogc[2]; T.fogc[2] = T.fogc[0]; T.fogc[0] = r; }
    }

    T.minx = (int)floorf(fminf(a[0], fminf(b[0], c[0])));
    T.maxx = (int)ceilf (fmaxf(a[0], fmaxf(b[0], c[0])));
    T.miny = (int)floorf(fminf(a[1], fminf(b[1], c[1])));
    T.maxy = (int)ceilf (fmaxf(a[1], fmaxf(b[1], c[1])));
    if (T.minx < (int)s_gpu.clip_x) T.minx = (int)s_gpu.clip_x;
    if (T.miny < (int)s_gpu.clip_y) T.miny = (int)s_gpu.clip_y;
    if (T.maxx > (int)(s_gpu.clip_x + s_gpu.clip_w)) T.maxx = (int)(s_gpu.clip_x + s_gpu.clip_w);
    if (T.maxy > (int)(s_gpu.clip_y + s_gpu.clip_h)) T.maxy = (int)(s_gpu.clip_y + s_gpu.clip_h);
    if (T.maxx > NV_ZBUF_W) T.maxx = NV_ZBUF_W;
    if (T.maxy > NV_ZBUF_H) T.maxy = NV_ZBUF_H;
    if (T.minx >= T.maxx || T.miny >= T.maxy) {
        s_gpu.tris_skipped_offscreen++;
        s_gpu.xf_offscreen++;
        return;
    }
    s_gpu.xf_drawn++;
    T.inv_area = 1.0f / area;

    xf_rows_parallel(&T, &cnt);
    s_gpu.xf_depth_fail += cnt.depth_fail;
    s_gpu.xf_pixels += cnt.pixels;
    s_gpu.zpass_count += cnt.zpass;
    s_gpu.tris_drawn++;
    note_drawn();
}


/* Near-plane clipping.
 *
 * A triangle with a vertex behind the eye (w <= 0) used to be dropped whole.
 * That is invisible for distant geometry and ruinous close up: in a chase
 * view the road under the camera is exactly the set of triangles that reach
 * behind it, so the bottom of the frame showed a hole -- a quarter of every
 * race frame's triangles went this way.
 *
 * Clipping has to happen in clip space, but the D3D epilogue has already
 * divided x, y and z by w and applied the viewport (c[58] scale, c[59]
 * offset) while leaving w itself alone. So each vertex is taken back to clip
 * space first: clip = (screen - offset) / scale * w. Everything else the
 * vertex carries -- colours, fog, texture coordinates -- is linear in clip
 * space and is interpolated directly. The polygon is clipped against
 * w = NV_CLIP_W (Sutherland-Hodgman, one plane), re-projected, and fanned.
 * ponytail: the near plane only; x/y/far fall to the rasteriser's bounds
 * check, which is correct, just slower for huge off-screen triangles. */
#define NV_CLIP_W 1e-3f

static void xf_to_clip(const Nv2aVshOutput *v, const float k[3],
                       const float o[3], float c[3])
{
    int i;
    for (i = 0; i < 3; i++)
        c[i] = k[i] != 0.0f ? (v->pos[i] - o[i]) / k[i] * v->pos[3] : 0.0f;
}

static void xf_lerp(const Nv2aVshOutput *a, const Nv2aVshOutput *b, float t,
                    Nv2aVshOutput *out)
{
    const float *pa = (const float *)a, *pb = (const float *)b;
    float *po = (float *)out;
    size_t i, n = sizeof(Nv2aVshOutput) / sizeof(float);
    for (i = 0; i < n; i++)
        po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

static void raster_xf_clipped(const Nv2aVshOutput *a, const Nv2aVshOutput *b,
                              const Nv2aVshOutput *c)
{
    const Nv2aVshOutput *in[3];
    Nv2aVshOutput poly[4], va, vb;
    float k[3], o[3], ca[3], cb[3];
    int n = 0, i, j;

    if (a->pos[3] > NV_CLIP_W && b->pos[3] > NV_CLIP_W && c->pos[3] > NV_CLIP_W) {
        raster_xf_triangle(a, b, c);
        return;
    }
    if (a->pos[3] <= NV_CLIP_W && b->pos[3] <= NV_CLIP_W
        && c->pos[3] <= NV_CLIP_W) {
        s_gpu.tris_behind++;                  /* wholly behind: nothing */
        return;
    }
    {
        const float *sc = nv2a_vsh_constant(58), *of = nv2a_vsh_constant(59);
        for (i = 0; i < 3; i++) { k[i] = sc[i]; o[i] = of[i]; }
    }
    in[0] = a; in[1] = b; in[2] = c;
    for (i = 0; i < 3; i++) {
        const Nv2aVshOutput *p = in[i], *q = in[(i + 1) % 3];
        int pin = p->pos[3] > NV_CLIP_W, qin = q->pos[3] > NV_CLIP_W;
        if (pin) {
            va = *p;
            xf_to_clip(p, k, o, ca);
            for (j = 0; j < 3; j++) va.pos[j] = ca[j];
            poly[n++] = va;
        }
        if (pin != qin) {
            float t = (NV_CLIP_W - p->pos[3]) / (q->pos[3] - p->pos[3]);
            va = *p; vb = *q;
            xf_to_clip(p, k, o, ca);
            xf_to_clip(q, k, o, cb);
            for (j = 0; j < 3; j++) { va.pos[j] = ca[j]; vb.pos[j] = cb[j]; }
            xf_lerp(&va, &vb, t, &poly[n++]);
        }
    }
    /* Back to screen space: divide by the (now positive) w, viewport. */
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            poly[i].pos[j] = poly[i].pos[j] / poly[i].pos[3] * k[j] + o[j];
    for (i = 1; i + 1 < n; i++)
        raster_xf_triangle(&poly[0], &poly[i], &poly[i + 1]);
}

static Nv2aVshOutput s_xf[NV_MAX_INDICES];

static int transform_vertex(uint32_t index, Nv2aVshOutput *out)
{
    float in[NV2A_VSH_INPUTS][4];
    uint32_t a;

    for (a = 0; a < NV2A_VSH_INPUTS; a++)
        fetch_attr(&s_gpu.attr[a], index, in[a]);   /* absent: 0,0,0,1 */
    return nv2a_vsh_run((const float (*)[4])in, out);
}

static void raster_batch_program(void)
{
    uint32_t i, n = s_gpu.idx_count;

    for (i = 0; i < n; i++)
        if (!transform_vertex(s_gpu.idx[i], &s_xf[i]))
            return;                                    /* no program loaded */
    s_gpu.batches_program++;
    s_gpu.verts_program += n;
    if (s_gpu.blend_enable) {
        uint32_t pair = s_gpu.blend_sfactor << 16 | (s_gpu.blend_dfactor & 0xFFFF);
        int j;
        for (j = 0; j < s_gpu.blend_npairs && s_gpu.blend_pairs[j] != pair; j++)
            ;
        if (j == s_gpu.blend_npairs && j < 16)
            s_gpu.blend_pairs[s_gpu.blend_npairs++] = pair;
    }
    {
        /* RECOMP_VSH_TRACE=<n>: print n program batches -- attribute setup,
         * raw inputs, key constants, and what came out. */
        static int left = -1;
        if (left < 0) {
            const char *t = getenv("RECOMP_VSH_TRACE");
            left = t ? atoi(t) : 0;
        }
        /* Spread out: one every 20000 program batches, so a run that spends
         * its first half in menus still traces the 3D scene. */
        if (left > 0 && n >= 3 && s_gpu.batches_program % 20000 == 0) {
            float in[4];
            left--;
            fprintf(stderr, "[VTRACE] prim %u n %u idx %u %u %u\n", s_gpu.prim,
                    n, s_gpu.idx[0], s_gpu.idx[1], s_gpu.idx[2]);
            for (i = 0; i < NV_VERTEX_ATTRS; i++) {
                const VertexAttr *at = &s_gpu.attr[i];
                if (!at->size)
                    continue;
                fetch_attr(at, s_gpu.idx[0], in);
                fprintf(stderr, "[VTRACE]   v%u off %08X type %u size %u"
                        " stride %u = %g %g %g %g\n", i, at->offset, at->type,
                        at->size, at->stride, in[0], in[1], in[2], in[3]);
            }
            {
                static const uint32_t cs[] = {58, 59, 96, 97, 112, 113, 114, 115};
                for (i = 0; i < 8; i++) {
                    const float *c = nv2a_vsh_constant(cs[i]);
                    fprintf(stderr, "[VTRACE]   c[%u] %g %g %g %g%c", cs[i],
                            c[0], c[1], c[2], c[3], 10);
                }
            }
            for (i = 0; i < 3; i++)
                fprintf(stderr, "[VTRACE]   out%u pos %g %g %g %g\n", i,
                        s_xf[i].pos[0], s_xf[i].pos[1], s_xf[i].pos[2],
                        s_xf[i].pos[3]);
        }
    }
    for (i = 0; i < NV2A_VSH_INPUTS; i++) {
        const VertexAttr *at = &s_gpu.attr[i];
        uint32_t f = at->type | (at->size << 4) | (i << 8), j;
        if (!at->size || !at->stride)
            continue;
        for (j = 0; j < (uint32_t)s_gpu.xf_nfmt && s_gpu.xf_fmt[j] != f; j++)
            ;
        if (j == (uint32_t)s_gpu.xf_nfmt && s_gpu.xf_nfmt < 16)
            s_gpu.xf_fmt[s_gpu.xf_nfmt++] = f;
    }
    if (s_gpu.texs[0].valid)
        note_texture_use();

    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3)
            raster_xf_clipped(&s_xf[i], &s_xf[i+1], &s_xf[i+2]);
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; i++)
            raster_xf_clipped(&s_xf[i], &s_xf[i+1], &s_xf[i+2]);
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < n; i++)
            raster_xf_clipped(&s_xf[0], &s_xf[i], &s_xf[i+1]);
        break;
    case NV_PRIM_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            raster_xf_clipped(&s_xf[i], &s_xf[i+1], &s_xf[i+2]);
            raster_xf_clipped(&s_xf[i], &s_xf[i+2], &s_xf[i+3]);
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < n; i += 2) {
            raster_xf_clipped(&s_xf[i], &s_xf[i+1], &s_xf[i+3]);
            raster_xf_clipped(&s_xf[i], &s_xf[i+3], &s_xf[i+2]);
        }
        break;
    default:
        break;
    }
}

static void raster_batch(void)
{
    uint32_t i;
    uint32_t before = s_gpu.tris_drawn;

    if (s_gpu.idx_count < 3 || !soft_raster_on())
        return;
    static int no_vsh = -1;
    if (no_vsh < 0)
        no_vsh = getenv("RECOMP_NO_VSH") != NULL;
    if ((s_gpu.xf_mode & 3) == 2 && !no_vsh) {
        int writes_constants;
        (void)nv2a_vsh_inputs_used(&writes_constants);
        /* A comparison pass must not execute context writes a second time. */
        if (writes_constants && getenv("RECOMP_PB_D3D11")) return;
        raster_batch_program();
        return;
    }
    if (!batch_is_screen_space()) {
        s_gpu.batches_untransformed++;
        return;
    }

    /* Count why, once per batch: the texture stage cannot change inside one. */
    {
        const VertexAttr *tc = texcoord_attr();

        if (!(tc->offset && tc->stride))
            s_gpu.batches_no_uv++;
        else if (!s_gpu.texs[0].valid)
            s_gpu.batches_no_tex++;
        else {
            s_gpu.batches_textured++;
            note_texture_use();
        }
    }

    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < s_gpu.idx_count; i += 3)
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2],
                           vertex_color(s_gpu.idx[i]));
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < s_gpu.idx_count; i++)
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2],
                           vertex_color(s_gpu.idx[i]));
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < s_gpu.idx_count; i++)
            raster_indexed(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1],
                           vertex_color(s_gpu.idx[0]));
        break;
    case NV_PRIM_QUADS:
        /* Independent quads, four vertices each. A batch of eight is two
         * quads, not one six-triangle fan around the first vertex; with
         * exactly four the two agreed, which is why sharing the fan arm
         * looked right. */
        for (i = 0; i + 3 < s_gpu.idx_count; i += 4) {
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2],
                           vertex_color(s_gpu.idx[i]));
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3],
                           vertex_color(s_gpu.idx[i]));
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        /* Each vertex pair past the first closes another quad against the
         * pair before it. */
        for (i = 0; i + 3 < s_gpu.idx_count; i += 2) {
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+3],
                           vertex_color(s_gpu.idx[i]));
            raster_indexed(s_gpu.idx[i], s_gpu.idx[i+3], s_gpu.idx[i+2],
                           vertex_color(s_gpu.idx[i]));
        }
        break;
    default:
        break;                             /* points and lines: not yet */
    }

    if (s_gpu.tris_drawn && (s_gpu.tris_drawn % 500) == 0 && !runtime_quiet_enabled())
        fprintf(stderr, "  [GPU] %u triangles rasterised\n", s_gpu.tris_drawn);

    /* Capture the surface while the geometry is still on it.
     *
     * The periodic report dumps too, but a title clears every frame and draws
     * in only some of them, so a report almost always lands on a surface that
     * was wiped a moment ago -- which reads as "nothing was drawn" when the
     * triangles went down correctly just before it. A few frames that actually
     * contain geometry are worth more than any number of clears. */
    if (s_gpu.tris_drawn != before && s_drawn_dumps < FB_DUMP_AFTER_DRAW
        && !s_flip_dumping) {
        s_drawn_dumps++;
        dump_surface_bmp();
    }
}

/* What a batch actually contains. Before anything can be rasterised, the
 * question is what space attribute 0 arrives in: a title running a vertex
 * program hands over object-space positions that mean nothing without running
 * it, while pre-transformed screen-space coordinates can be drawn directly. */
/* RECOMP_FRAME_TRACE=<flag file>: once the file exists, log every batch of
 * the next whole frame (flip to flip) -- where it drew, with what texture,
 * blend and depth state, and how many pixels it actually wrote. "The frame
 * is black" has many causes and this is what tells them apart. */

static void frame_trace_flip(void)
{
    static const char *flag = (const char *)-1;
    if (flag == (const char *)-1)
        flag = getenv("RECOMP_FRAME_TRACE");
    if (!flag)
        return;
    if (s_ftrace == 3) {
        /* Deleting the flag file re-arms the trace for the next time it
         * appears, so one run can trace several moments. */
        FILE *f = fopen(flag, "rb");
        if (f)
            fclose(f);
        else
            s_ftrace = 0;
        return;
    }
    if (s_ftrace == 2) {
        s_ftrace = 3;
        fprintf(stderr, "[FTRACE] end of frame\n");
        return;
    }
    if (s_ftrace == 0) {
        FILE *f = fopen(flag, "rb");
        if (!f)
            return;
        fclose(f);
        s_ftrace = 2;
        fprintf(stderr, "[FTRACE] frame begins (flip %u)\n", s_gpu.flips);
    }
}

/* Hand the bound texture to the translator, expanding palettised ones.
 *
 * SZ_I8_A8R8G8B8 is an 8-bit index per texel into a palette of ARGB entries
 * the title points at with SET_TEXTURE_PALETTE. The translator's D3D8 layer
 * knows the format only as P8 with a palette of its own, which defaults to a
 * grey ramp -- so an indexed texture came out as grey noise, which is how the
 * first screen after this title's loading screen looked. The expansion is
 * done here, where the palette address is known: unswizzle the indices,
 * look each one up, and send linear ARGB.
 *
 * The translator caches an upload by (address, format, size), so the key it
 * is given folds in the palette address, and the expansion itself is skipped
 * for a key already sent -- a menu binds the same texture hundreds of times a
 * frame. It is skipped only while the indices and palette are unchanged
 * (pal_tex_sig, checked at most every 8 ms per key) and the translator still
 * holds the key. Def Jam draws its text into I8 textures and rewrites them in
 * place -- the name-entry field as each letter is typed -- and the
 * translator's cache evicts; skipping on the key alone showed stale text and,
 * after an eviction, bound nothing. */
static uint32_t pal_tex_sig(const uint8_t *idx, uint32_t n,
                            const uint32_t *pal, uint32_t pal_len)
{
    /* Every index word of a texture up to 64 KB (a text line is 16 KB, and one
     * changed glyph moves only a few words); every 17th above that. */
    const uint32_t *w = (const uint32_t *)idx;
    uint32_t words = n / 4, step = n <= 65536 ? 1 : 17, i;
    uint32_t h = 2166136261u;
    for (i = 0; i < words; i += step)
        h = (h ^ w[i]) * 16777619u;
    for (i = 0; i < pal_len; i++)
        h = (h ^ pal[i]) * 16777619u;
    return h;
}

/* RECOMP_PB_SURFACE_TRACE=1: every colour surface the title draws into, and
 * any texture it then samples from one -- the question being whether a screen
 * is rendered off-screen and composited, which the D3D11 path (one back
 * buffer) does not model. */
static uint32_t s_surf_addr[32];
static unsigned s_surf_draws[32], s_surf_n;
static int surf_trace_on(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("RECOMP_PB_SURFACE_TRACE") != NULL;
    return on;
}
static void surf_note_target(uint32_t addr)
{
    unsigned i;
    for (i = 0; i < s_surf_n; i++)
        if (s_surf_addr[i] == addr) {
            if ((++s_surf_draws[i] & (s_surf_draws[i] - 1)) == 0 && s_surf_draws[i] >= 64
                    && surf_trace_on())
                fprintf(stderr, "  [SURF] colour surface 0x%08X selected %u times%c",
                        addr, s_surf_draws[i], 10);
            return;
        }
    if (s_surf_n < 32) {
        s_surf_addr[s_surf_n] = addr;
        s_surf_draws[s_surf_n++] = 1;
        if (surf_trace_on())
            fprintf(stderr, "  [SURF] new colour surface 0x%08X (pitch %u, clip %ux%u, format 0x%X)%c",
                addr, s_gpu.pitch, s_gpu.clip_w, s_gpu.clip_h, s_gpu.format, 10);
    }
}
/* Whether a texture lies in a colour surface the title has drawn into. The
 * D3D11 path draws every surface into its one back buffer and never writes
 * guest memory, so sampling one reads what the title's CPU left there --
 * black, for Def Jam's fight, which draws its 3D into 0x82A90000 and
 * composites it with a full-screen quad sampling that address. */
static int surf_holds_texture(uint32_t tex_addr)
{
    unsigned i;
    for (i = 0; i < s_surf_n; i++) {
        uint32_t a = s_surf_addr[i] & 0x0FFFFFFFu, t = tex_addr & 0x0FFFFFFFu;
        if (t >= a && t < a + 0x200000u && s_surf_draws[i] > 1)
            return 1;
    }
    return 0;
}

static void surf_note_texture(uint32_t tex_addr, uint32_t w, uint32_t h, uint32_t fmt)
{
    static uint32_t told[32];
    static unsigned told_n;
    unsigned i, k;
    for (i = 0; i < s_surf_n; i++) {
        uint32_t a = s_surf_addr[i] & 0x0FFFFFFFu, t = tex_addr & 0x0FFFFFFFu;
        if (t < a || t >= a + 0x200000u)
            continue;
        for (k = 0; k < told_n; k++)
            if (told[k] == tex_addr)
                return;
        if (told_n < 32)
            told[told_n++] = tex_addr;
        fprintf(stderr, "  [SURF] texture 0x%08X (%ux%u, format 0x%X) lies in colour surface 0x%08X%c",
                tex_addr, w, h, fmt, s_surf_addr[i], 10);
        return;
    }
}

static int pack_sample(void *user, uint32_t x, uint32_t y, uint32_t *argb)
{
    return sample_tex((const Texture *)user, 0, x, y, argb);
}

static int try_texture_pack(const Texture *t, const uint8_t *mem)
{
    RecompTextureSource source={0};
    uint32_t block=d3d8_format_dxt_block_bytes(t->color), bpp=d3d8_format_bpp(t->color)/8;
    uint64_t bytes;
    if (!texture_pack_active() || !t->valid || t->cube || surf_holds_texture(t->offset) ||
        !t->width || !t->height || t->width>4096 || t->height>4096) return 0;
    /* Static colour formats only. Video/YUV and depth formats retain their live path. */
    if (!block && t->color!=0x06 && t->color!=0x07 && t->color!=0x0b && t->color!=0x12 && t->color!=0x1e) return 0;
    source.address=t->offset; source.format=t->color; source.width=t->width; source.height=t->height;
    source.row_texels=linear_row_texels(t); source.data=mem+t->offset;
    if (block) bytes=(uint64_t)((t->width+3)/4)*((t->height+3)/4)*block;
    else if (tex_size_from_format(t->color)) bytes=(uint64_t)t->width*t->height*bpp;
    else {
        source.stride=t->pitch; source.row_bytes=t->width*bpp; source.rows=t->height;
        if (source.stride<source.row_bytes) return 0;
        bytes=(uint64_t)(t->height-1)*t->pitch+source.row_bytes;
    }
    if (!bytes || bytes>64u*1024u*1024u) return 0;
    source.bytes=(uint32_t)bytes;
    if (t->color==0x0b) {
        source.palette=mem+t->palette; source.palette_entries=t->palette_len;
        if (!t->palette || !t->palette_len || t->palette_len>256) return 0;
    }
    return pgraph_d3d11_try_texture_pack(&source,pack_sample,(void *)t);
}

static int s_texture_alias_failed;
static int s_capture_stage0_alias; /* confirmed dimensions before diagnostic readback */
/* Retain only the original, addressable chain. Live render-target aliases
 * and texture packs are resolved before this helper is used. Cube/P8/linear
 * layouts keep their existing paths. Own compatibility work: djultra64. */
static uint32_t ready_texture_levels(const Texture *t)
{
    uint32_t block=d3d8_format_dxt_block_bytes(t->color);
    if(t->color==NV097_TEXFMT_SZ_I8_A8R8G8B8
       ||(!block&&!d3d8_format_is_swizzled(t->color)))return 1;
    if(!t->width||!t->height||t->width>4096||t->height>4096)return 0;
    uint32_t bpp=d3d8_format_bpp(t->color)/8;
    if(!block&&!bpp)return 1;
    uint32_t largest=t->width>t->height?t->width:t->height,max_levels=1;
    while(largest>1){largest>>=1;max_levels++;}
    uint32_t levels=t->levels?t->levels:1;
    if(levels>max_levels)levels=max_levels;
    /* MAX_LOD_CLAMP is the integer field in NV097_TEXTURE_CONTROL0[17:6]. */
    unsigned stage=(unsigned)(t-s_gpu.texs);
    uint32_t max_lod=(s_gpu.texture_control[stage]>>6)&0xfffu;
    if(levels>max_lod+1)levels=max_lod+1;
    uint64_t total=0,end;
    for(uint32_t level=0;level<levels;level++){
        uint32_t w=t->width>>level,h=t->height>>level;
        if(!w)w=1;if(!h)h=1;
        uint64_t bytes=block?(uint64_t)((w+3)/4)*((h+3)/4)*block:(uint64_t)w*h*bpp;
        end=(uint64_t)t->offset+total+bytes;
        if(!((t->offset>=0x10000u&&end<=0x04000000ull)
             ||(t->offset>=0x80000000u&&end<=0x84000000ull)))return level;
        total+=bytes;
    }
    return levels;
}
static void bind_texture_ready(const Texture *t)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t w = t->width, h = t->height;
    enum { SEEN_MAX = 512 };
    static struct {
        uint32_t key, sig;
        ULONGLONG checked_ms;
    } seen[SEEN_MAX];
    static int seen_count, seen_next;
    uint32_t key, sig, *argb;
    uint8_t *idx;
    const uint32_t *pal;
    uint32_t i, n;
    int slot = -1;
    ULONGLONG now;

    int alias=t->cube?0:pgraph_d3d11_try_surface_texture(t->offset,w,h,t->color);
    if(t==&s_gpu.texs[0])s_capture_stage0_alias=alias==1;
    if(alias) { if(alias<0)s_texture_alias_failed=1; return; }
    if(t->cube){
        /* Never replace six directional faces by a2D alias or first face.
         * Face stride uses original FORMAT levels, before sample LOD clamp. */
        uint32_t levels=ready_texture_levels(t),stride=tex_face_stride(t);
        uint64_t end=(uint64_t)t->offset+(uint64_t)stride*6;
        if(!levels||w!=h||!w||(w&(w-1))||t->color==NV097_TEXFMT_SZ_I8_A8R8G8B8
           ||(!d3d8_format_dxt_block_bytes(t->color)&&!d3d8_format_is_swizzled(t->color))
           ||!((t->offset>=0x10000u&&end<=0x04000000ull)
                ||(t->offset>=0x80000000u&&end<=0x84000000ull))
           ||!pgraph_d3d11_set_cube_texture_ready(t->offset,mem+t->offset,w,t->color,levels,stride))
            pgraph_d3d11_set_texture_ready(0,NULL,0,0,0);
        return;
    }
    if (try_texture_pack(t,mem)) return;

    if (surf_trace_on())
        surf_note_texture(t->offset, w, h, t->color);
    {
        /* RECOMP_TEX_FORMATS=1: the first texture of each format, once. */
        static int on = -1;
        static uint8_t seen_fmt[256];
        if (on < 0)
            on = getenv("RECOMP_TEX_FORMATS") != NULL;
        if (on && !seen_fmt[t->color & 0xFF]) {
            seen_fmt[t->color & 0xFF] = 1;
            fprintf(stderr, "  [TEXFMT] format 0x%02X first at 0x%08X, %ux%u, pitch %u, palette 0x%08X (%u)%c",
                    t->color, t->offset, w, h, t->pitch, t->palette,
                    t->palette_len, 10);
        }
        /* And every distinct linear texture (first 64), with its pitch: a
         * linear texture's rows are pitch bytes apart, not width * bpp. */
        if (on && !tex_size_from_format(t->color)) {
            static uint32_t lin_seen[64];
            static int lin_n;
            int k;
            for (k = 0; k < lin_n && lin_seen[k] != t->offset; k++)
                ;
            if (k == lin_n && lin_n < 64) {
                lin_seen[lin_n++] = t->offset;
                fprintf(stderr, "  [TEXFMT] linear 0x%02X at 0x%08X, %ux%u, pitch %u, row texels %u%c",
                        t->color, t->offset, w, h, t->pitch,
                        linear_row_texels(t), 10);
            }
        }
    }
    if (t->color != NV097_TEXFMT_SZ_I8_A8R8G8B8 || !t->palette
            || !w || !h || w > 4096 || h > 4096) {
        uint32_t levels=ready_texture_levels(t);
        if(!levels){pgraph_d3d11_set_texture_ready(0,NULL,0,0,0);return;}
        pgraph_d3d11_set_texture_mips_ready(t->offset,mem+t->offset,
                                           linear_row_texels(t),h,t->color,levels);
        return;
    }

    key = t->offset ^ (t->palette * 2654435761u);
    n = w * h;
    pal = (const uint32_t *)(mem + t->palette);
    now = GetTickCount64();
    sig = 0;
    for (i = 0; i < (uint32_t)seen_count; i++) {
        if (seen[i].key != key)
            continue;
        slot = (int)i;
        static int recheck = -1;
        if (recheck < 0)
            recheck = getenv("RECOMP_PAL_RECHECK") != NULL;
        if (!recheck && now - seen[i].checked_ms < 8) {
            sig = seen[i].sig;
        } else {
            sig = pal_tex_sig(mem + t->offset, n, pal, t->palette_len);
            seen[i].checked_ms = now;
        }
        /* Unchanged: bind by key. No data, because the translator's own
         * change check would read indices where it holds ARGB. */
        if (sig == seen[i].sig
                && pgraph_d3d11_set_texture_ready(key, NULL, w, h,
                                                  NV097_TEXFMT_LU_IMAGE_A8R8G8B8))
            return;
        break;
    }
    if (slot < 0)
        sig = pal_tex_sig(mem + t->offset, n, pal, t->palette_len);

    idx = (uint8_t *)malloc(n);
    argb = (uint32_t *)malloc((size_t)n * 4);
    if (!idx || !argb) {
        free(idx); free(argb);
        pgraph_d3d11_set_texture_ready(0, NULL, 0, 0, 0);
        return;
    }
    xbox_unswizzle_rect(idx, mem + t->offset, w, h, 1);
    for (i = 0; i < n; i++)
        argb[i] = idx[i] < t->palette_len ? pal[idx[i]] : 0;
    pgraph_d3d11_replace_texture_ready(key, argb, w, h, NV097_TEXFMT_LU_IMAGE_A8R8G8B8);
    {
        /* RECOMP_PAL_DUMP=<texture address>[~<mask>],<file>: the ARGB this path
         * made of that palettised texture, as a BMP -- what is uploaded,
         * rewritten each time it is expanded again, so the file is its latest
         * state. With a mask, every texture whose address matches under it
         * goes to <file>-<address>.bmp. */
        const char *e = getenv("RECOMP_PAL_DUMP");
        static int done;
        char *comma, *slash = NULL;
        uint32_t want = e ? (uint32_t)strtoul(e, &slash, 0) : 0;
        uint32_t mask = (slash && *slash == '~') ? (uint32_t)strtoul(slash + 1, NULL, 0) : 0xFFFFFFFFu;
        if (e && (comma = strchr(e, ',')) != NULL
                && (t->offset & mask) == (want & mask)) {
            char name[600];
            FILE *f;
            if (mask == 0xFFFFFFFFu)
                snprintf(name, sizeof name, "%s", comma + 1);
            else
                snprintf(name, sizeof name, "%s-%08X.bmp", comma + 1, t->offset);
            f = fopen(name, "wb");
            if (f) {
                uint8_t hdr[54];
                uint32_t filesz = 54 + w * h * 4, x, y;
                memset(hdr, 0, sizeof hdr);
                hdr[0] = 'B'; hdr[1] = 'M';
                memcpy(hdr + 2, &filesz, 4);
                hdr[10] = 54; hdr[14] = 40;
                memcpy(hdr + 18, &w, 4);
                memcpy(hdr + 22, &h, 4);
                hdr[26] = 1; hdr[28] = 32;
                fwrite(hdr, 1, sizeof hdr, f);
                for (y = h; y-- > 0;)
                    for (x = 0; x < w; x++)
                        fwrite(&argb[y * w + x], 4, 1, f);
                fclose(f);
                if (done++ < 8)
                    fprintf(stderr, "  [PAL] wrote 0x%08X (%ux%u, palette 0x%08X) to %s%c",
                            t->offset, w, h, t->palette, name, 10);
            }
        }
    }
    free(idx);
    free(argb);

    if (slot < 0) {
        if (seen_count < SEEN_MAX) {
            slot = seen_count++;
        } else {
            slot = seen_next;
            seen_next = (seen_next + 1) % SEEN_MAX;
        }
        seen[slot].key = key;
    }
    seen[slot].sig = sig;
    seen[slot].checked_ms = now;
    {
        static uint32_t expanded;
        if (++expanded % 1000 == 0 && !runtime_quiet_enabled())
            fprintf(stderr, "  [GPU] palettised textures expanded: %u\n", expanded);
    }
    {
        static int shown;
        if (shown++ < 8)
            fprintf(stderr, "  [GPU] palettised texture 0x%08X %ux%u expanded "
                            "through palette 0x%08X (%u entries)\n",
                    t->offset, w, h, t->palette, t->palette_len);
    }
}

/* Bind the canonical stages without mutating another stage's state. */
static void bind_extra_stages(int with_coords)
{
    int st;
    for (st = 1; st < 4; st++) {
        const Texture *t = &s_gpu.texs[st];
        if (with_coords && s_tex_set[st * 16 + 3] && t->valid) {
            pgraph_d3d11_bind_stage(st);
            bind_texture_ready(t);
            pgraph_d3d11_set_texture_address(t->addr_u, t->addr_v);
            pgraph_d3d11_set_texture_filter(t->filter);
            pgraph_d3d11_bind_stage(0);
        } else {
            pgraph_d3d11_unbind_stage(st);
        }
    }
}

/* Top Spin's fixed UI sends object-space float3 vertices and programs the
 * NV097 composite matrix with its viewport already folded in. Keep this
 * separate from program constants and from mode 1's pretransformed vertices. */
static int fixed_transform_method(uint32_t method, uint32_t param)
{
    if(method>=0x480&&method<0x4c0&&!(method&3)) {
        unsigned i=(method-0x480)/4;memcpy(&s_gpu.fog_modelview[i],&param,4);s_gpu.fog_modelview_written|=(uint16_t)(1u<<i);return 1;
    }
    if(method>=0x9d0&&method<0x9e0&&!(method&3)) {
        memcpy(&s_gpu.fog_plane[(method-0x9d0)/4],&param,4);return 1;
    }
    if (method < 0x680 || method >= 0x6C0 || (method & 3))
        return 0;
    unsigned i = (method - 0x680) / 4;
    memcpy(&s_gpu.fixed_composite[i], &param, sizeof param);
    s_gpu.fixed_composite_written |= (uint16_t)(1u << i);
    return 1;
}

static float fixed_fog_factor(uint32_t index,const float position[4]) {
    if(!s_gpu.fog_enable)return 1.f;
    float attr[4],distance=0,eye[3];
    for(unsigned row=0;row<3;row++) {
        eye[row]=position[row];
        if(s_gpu.fog_modelview_written==0xffffu) {
            const float*m=s_gpu.fog_modelview+row*4;
            eye[row]=m[0]*position[0]+m[1]*position[1]+m[2]*position[2]+m[3]*position[3];
        }
    }
    switch(s_gpu.fog_gen) {
    case 0: if(!fetch_attr(&s_gpu.attr[4],index,attr))nv2a_vsh_current(4,attr);distance=fmaxf(0,fminf(1,attr[3]));break;
    case 1:distance=sqrtf(eye[0]*eye[0]+eye[1]*eye[1]+eye[2]*eye[2]);break;
    case 2:case 3:distance=s_gpu.fog_plane[0]*eye[0]+s_gpu.fog_plane[1]*eye[1]+s_gpu.fog_plane[2]*eye[2]+s_gpu.fog_plane[3];if(s_gpu.fog_gen==3)distance=fabsf(distance);break;
    case 6:if(!fetch_attr(&s_gpu.attr[5],index,attr))nv2a_vsh_current(5,attr);distance=attr[0];break;
    }
    return d3d8_nv_fog_factor(distance,1,s_gpu.fog_mode,s_gpu.fog_param[0],s_gpu.fog_param[1]);
}

static void fixed_position_ready(const float pos[4], PgraphReadyVertex *out)
{
    float transformed[4];
    out->x = pos[0]; out->y = pos[1]; out->z = pos[2]; out->w = 1.0f;
    /* A title that supplied no complete composite retains the old screen
     * path. Program mode and passthrough mode must never consume stale fixed
     * state, even when the matrix remains programmed across mode switches. */
    if ((s_gpu.xf_mode & 3u) != 0 || s_gpu.fixed_composite_written != 0xFFFFu)
        return;
    for (unsigned row = 0; row < 4; row++) {
        const float *m = &s_gpu.fixed_composite[row * 4];
        transformed[row] = m[0] * pos[0] + m[1] * pos[1]
                         + m[2] * pos[2] + m[3] * pos[3];
    }
    /* Ready vertices carry post-divide screen positions plus clip W. The
     * receiving path handles clipping and depth normalization. */
    float inv_w = fabsf(transformed[3]) > 1e-20f ? 1.0f / transformed[3] : 1.0f;
    out->x = transformed[0] * inv_w;
    out->y = transformed[1] * inv_w;
    out->z = transformed[2] * inv_w;
    out->w = transformed[3];
}

/* One-shot title material evidence. Default off; no guest texture mutation.
 * The host readback flushes queued producer writes; diagnostic timing only.
 * Own investigation: djultra64. */
static size_t s_crowd_capture_bytes;
static int crowd_capture_file(const char *base,const char *suffix,const void *data,size_t bytes)
{
    char name[768];FILE*f;int ok;
    if(bytes>600u*1024u-s_crowd_capture_bytes)return 0;
    s_crowd_capture_bytes+=bytes;
    if(snprintf(name,sizeof name,"%s-%s",base,suffix)>=(int)sizeof name)return 0;
    f=fopen(name,"wb");if(!f)return 0;
    ok=fwrite(data,1,bytes,f)==bytes; if(fclose(f))ok=0;return ok;
}
static void crowd_capture_payload(const float *vertices,uint32_t nverts,uint32_t used,
                                  const uint16_t *indices,uint32_t nindices)
{
    static int initialized,enabled;static const char *prefix;
    static unsigned limit=1,count;static uint32_t min_flip=400,seen[4];
    const Texture*t=&s_gpu.texs[0],*lookup=&s_gpu.texs[2];
    uint32_t per=0,start,revision,version;size_t vertex_bytes;
    const Nv2aVshInstruction *program;const float *constants;
    Nv2aVshOutput *oracle=NULL;uint8_t *rgba=NULL;char *hlsl=NULL;
    char base[640],metadata[8192];size_t meta_used=0;int meta_ok=1,ok=1,readback,cpu_ok=1;
    if(!initialized){
        const char*e;char*end;unsigned long v;
        initialized=1;prefix=getenv("RECOMP_CROWD_CAPTURE_PREFIX");enabled=prefix&&*prefix;
        e=getenv("RECOMP_CROWD_CAPTURE_MAX");if(e&&*e){v=strtoul(e,&end,10);if(!*end&&v>=1&&v<=4)limit=(unsigned)v;}
        e=getenv("RECOMP_CROWD_CAPTURE_MIN_FLIP");if(e&&*e){v=strtoul(e,&end,10);if(!*end&&v<=UINT32_MAX)min_flip=(uint32_t)v;}
    }
    if(!enabled||count>=limit||s_gpu.flips<min_flip||s_gpu.rc.stage_program!=0x02621
       ||!t->valid||t->color!=0x05||t->width!=64||t->height!=128
       ||!lookup->valid||lookup->color!=0x05||lookup->width!=64||lookup->height!=16
       ||!nverts||nverts>1024||!nindices||nindices>6144)return;
    /* The existing readback ABI has no destination height argument. Only
     * read the exact resource whose 64x128 alias was validated immediately
     * before this hook, preventing a resized allocation from overrunning it. */
    if(!s_capture_stage0_alias){
        static int warned;if(!warned++){fprintf(stderr,"[CROWD-CAPTURE] no validated 64x128 GPU producer alias; capture deferred\n");}
        return;
    }
    for(unsigned i=0;i<count;i++)if(seen[i]==t->offset)return;
    seen[count]=t->offset;
    if(snprintf(base,sizeof base,"%s-%u",prefix,count)>=(int)sizeof base){count=limit;return;}
    count++;s_crowd_capture_bytes=0;
    for(unsigned i=0;i<16;i++)per+=(used>>i)&1u;if(!per)per=1;
    vertex_bytes=(size_t)nverts*per*4*sizeof(float);
    rgba=(uint8_t*)malloc(64*128*4);oracle=(Nv2aVshOutput*)calloc(nverts,sizeof *oracle);hlsl=(char*)malloc(96*1024);
    if(!rgba||!oracle||!hlsl){fprintf(stderr,"[CROWD-CAPTURE] allocation failed %s\n",base);goto done;}
    readback=pgraph_d3d11_diagnostic_readback(t->offset,rgba,64*4);
    if(readback==1)ok&=crowd_capture_file(base,"producer.rgba",rgba,64*128*4);else ok=0;
    program=nv2a_vsh_program_data(&revision,&start);constants=nv2a_vsh_constants(&version);
    ok&=crowd_capture_file(base,"program.u32",program,NV2A_VSH_SLOTS*sizeof *program);
    ok&=crowd_capture_file(base,"constants.f32",constants,NV2A_VSH_CONSTANTS*4*sizeof(float));
    ok&=crowd_capture_file(base,"vertices.f32",vertices,vertex_bytes);
    ok&=crowd_capture_file(base,"indices.u16",indices,(size_t)nindices*sizeof *indices);
    ok&=crowd_capture_file(base,"lookup.rgb565",(const uint8_t*)xbox_GetMemoryOffset()+lookup->offset,64*16*2);
    ok&=crowd_capture_file(base,"combiner.bin",&s_gpu.rc,sizeof s_gpu.rc);
    ok&=crowd_capture_file(base,"render-state.u32",s_rs,sizeof s_rs);
    ok&=crowd_capture_file(base,"texture-registers.u32",s_tex_reg,sizeof s_tex_reg);
    ok&=crowd_capture_file(base,"texture-registers-known.u8",s_tex_set,sizeof s_tex_set);
    for(uint32_t i=0;i<nverts;i++){
        float in[16][4]={{0}};unsigned o=0;
        for(unsigned a=0;a<16;a++){
            in[a][3]=1;
            if((used>>a)&1u){memcpy(in[a],vertices+(size_t)i*per*4+o,4*sizeof(float));o+=4;}
        }
        if(!nv2a_vsh_run((const float(*)[4])in,&oracle[i]))cpu_ok=0;
    }
    ok&=crowd_capture_file(base,"cpu-outputs.f32",oracle,(size_t)nverts*sizeof *oracle);
    {uint32_t emitted_used=0;int bytes=nv2a_vsh_emit_hlsl(hlsl,96*1024,&emitted_used);
     if(bytes>0&&emitted_used==used)ok&=crowd_capture_file(base,"vertex.hlsl",hlsl,(size_t)bytes);else ok=0;}
#define CROWD_META(...) do { if(meta_ok) { int written=snprintf(metadata+meta_used,sizeof metadata-meta_used,__VA_ARGS__); if(written<0 || (size_t)written>=sizeof metadata-meta_used)meta_ok=0; else meta_used+=(size_t)written; } } while(0)
    {
        CROWD_META("crowd_capture_version=1\nauthor=djultra64\nflip=%u\ndraw=%u\nused=%08X\nnverts=%u\nnindices=%u\nvertex_stride_floats=%u\nprogram_hash=%08X\nprogram_start=%u\nprogram_revision=%u\nconstant_revision=%u\nproducer_readback=%d\ncpu_oracle_ok=%d\n",
          s_gpu.flips,s_gpu.draws,used,nverts,nindices,per*4,nv2a_vsh_program_hash(),start,revision,version,readback,cpu_ok);
        CROWD_META("surface=%08X zeta=%08X format=%08X pitch=%u/%u clip=%u,%u,%u,%u\nxf_mode=%08X\ncombiner_control=%08X stage_program=%08X final=%08X/%08X\ndot_mapping=%08X other_stage_input=%08X control_words_known=%04X\n",
          s_gpu.color_offset,s_gpu.zeta_offset,s_gpu.format,s_gpu.pitch,s_gpu.zeta_pitch,s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.xf_mode,
          s_gpu.rc.control,s_gpu.rc.stage_program,s_gpu.rc.final0,s_gpu.rc.final1,s_cmb_c[13],s_cmb_c[14],s_cmb_c_known);
        CROWD_META("alpha=%u func=%X ref=%u blend=%u func=%X/%X eq=%X constant=%08X color_mask=%08X depth=%u func=%X write=%u fog=%u mode=%X color=%08X params=%a,%a\n",
          s_gpu.alpha_test,s_gpu.alpha_func,s_gpu.alpha_ref,s_gpu.blend_enable,s_gpu.blend_sfactor,s_gpu.blend_dfactor,s_gpu.blend_equation,s_gpu.blend_color,s_gpu.color_mask,s_gpu.depth_test,s_gpu.depth_func,s_gpu.depth_mask,s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_color,(double)s_gpu.fog_param[0],(double)s_gpu.fog_param[1]);
        for(unsigned a=0;a<4;a++){const Texture*q=&s_gpu.texs[a];CROWD_META("texture%u=%08X size=%ux%u pitch=%u format=%02X filter=%08X address=%X/%X cube=%d levels=%u valid=%d control=%08X key=%08X key_known=%u\n",a,q->offset,q->width,q->height,q->pitch,q->color,q->filter,q->addr_u,q->addr_v,q->cube,q->levels,q->valid,s_gpu.texture_control[a],s_gpu.texture_key[a],s_gpu.texture_key_known[a]);}
        for(unsigned a=0;a<8;a++)CROWD_META("combiner%u=%08X/%08X outputs=%08X/%08X constants=%08X/%08X\n",a,s_gpu.rc.color_icw[a],s_gpu.rc.alpha_icw[a],s_gpu.rc.color_ocw[a],s_gpu.rc.alpha_ocw[a],s_gpu.rc.factor0[a],s_gpu.rc.factor1[a]);
        CROWD_META("files_ok=%d\nraw_lookup_layout=guest_swizzled_rgb565\nproducer_layout=rgba8_top_left\n",ok);if(meta_ok)ok&=crowd_capture_file(base,"state.txt",metadata,meta_used);else ok=0;
    }
#undef CROWD_META
    fprintf(stderr,"[CROWD-CAPTURE] %s flip=%u draw=%u producer=%08X lookup=%08X vertices=%u indices=%u GPUreadback=%d files=%s oracle=%d\n",base,s_gpu.flips,s_gpu.draws,t->offset,lookup->offset,nverts,nindices,readback,ok?"ok":"FAILED",cpu_ok);
done:
    free(hlsl);free(oracle);free(rgba);
}

/* Directed native720 stadium evidence. Default off; CPU oracle only, never
 * changes the draw or substitutes its outputs. Own investigation: djultra64. */
static size_t s_stadium_capture_bytes;
static size_t s_stadium_capture_budget=2u*1024u*1024u;
static int stadium_capture_file(const char *base,const char *suffix,const void *data,size_t bytes)
{
    char name[768];FILE*f;int ok;
    if(bytes>s_stadium_capture_budget-s_stadium_capture_bytes)return 0;
    s_stadium_capture_bytes+=bytes;
    if(snprintf(name,sizeof name,"%s-%s",base,suffix)>=(int)sizeof name)return 0;
    f=fopen(name,"wb");if(!f)return 0;
    ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=0;return ok;
}
static void stadium_capture_payload(const float *vertices,uint32_t nverts,uint32_t used,
                                  const uint16_t *indices,uint32_t nindices)
{
    static int initialized,enabled,point_mode;static const char *prefix;
    static unsigned limit=4,count;static uint32_t min_flip=500,seen[8],capture_flip=UINT32_MAX;static float point_x=310,point_y=40;
    const Texture*t=&s_gpu.texs[0];
    uint32_t per=0,start,revision,version,point_hits=0,first_triangle=UINT32_MAX;size_t vertex_bytes;
    const Nv2aVshInstruction *program;const float *constants;
    Nv2aVshOutput *oracle=NULL;char *hlsl=NULL;
    char base[640],metadata[8192];size_t meta_used=0;int meta_ok=1,ok=1,cpu_ok=1;
    if(!initialized){
        const char*e;char*end;unsigned long v;
        initialized=1;prefix=getenv("RECOMP_STADIUM_CAPTURE_PREFIX");enabled=prefix&&*prefix;
        e=getenv("RECOMP_STADIUM_CAPTURE_MODE");point_mode=e&&!strcmp(e,"point");
        if(point_mode){limit=8;s_stadium_capture_budget=8u*1024u*1024u;point_x=490;point_y=70;}
        e=getenv("RECOMP_STADIUM_CAPTURE_POINT");if(e){float x,y;char tail;if(sscanf(e,"%f,%f%c",&x,&y,&tail)==2&&isfinite(x)&&isfinite(y)){point_x=x;point_y=y;}}
        e=getenv("RECOMP_STADIUM_CAPTURE_MIN_FLIP");if(e&&*e){v=strtoul(e,&end,10);if(!*end&&v<=UINT32_MAX)min_flip=(uint32_t)v;}
    }
    if(!enabled||count>=limit||s_gpu.flips<min_flip||(!point_mode&&s_gpu.rc.stage_program!=0x0461)
       ||(capture_flip!=UINT32_MAX&&s_gpu.flips!=capture_flip)
       ||(!point_mode&&!t->valid)||!nverts||nverts>1024||!nindices||nindices>6144)return;
    if(!point_mode&&t->offset!=0x80F96000u&&t->offset!=0x80FA9700u
       &&t->offset!=0x81029600u&&t->offset!=0x8137C880u)return;
    if(!point_mode)for(unsigned i=0;i<count;i++)if(seen[i]==t->offset)return;
    if(point_mode&&(!s_gpu.color_offset||((s_gpu.format&0xffu)!=0x28u)
       ||point_x+.5<s_gpu.clip_x||point_y+.5<s_gpu.clip_y
       ||point_x+.5>=s_gpu.clip_x+s_gpu.clip_w
       ||point_y+.5>=s_gpu.clip_y+s_gpu.clip_h))return;
    {int writes=0;nv2a_vsh_inputs_used(&writes);if(writes)return;}
    capture_flip=s_gpu.flips;
    if(snprintf(base,sizeof base,"%s-%u",prefix,count)>=(int)sizeof base){count=limit;return;}
    for(unsigned i=0;i<16;i++)per+=(used>>i)&1u;if(!per)per=1;
    vertex_bytes=(size_t)nverts*per*4*sizeof(float);
    oracle=(Nv2aVshOutput*)calloc(nverts,sizeof *oracle);hlsl=(char*)malloc(96*1024);
    if(!oracle||!hlsl){fprintf(stderr,"[STADIUM-CAPTURE] allocation failed %s\n",base);goto done;}
    program=nv2a_vsh_program_data(&revision,&start);constants=nv2a_vsh_constants(&version);
    for(uint32_t i=0;i<nverts;i++){
        float in[16][4]={{0}};unsigned o=0;
        for(unsigned a=0;a<16;a++){
            in[a][3]=1;
            if((used>>a)&1u){memcpy(in[a],vertices+(size_t)i*per*4+o,4*sizeof(float));o+=4;}
        }
        if(!nv2a_vsh_run((const float(*)[4])in,&oracle[i]))cpu_ok=0;
    }
    if(point_mode){
        /* Screen-space sample center. Excludes W-crossing triangles rather
         * than pretending finite projected bounds prove actual clipping. */
        const double px=(double)point_x+.5,py=(double)point_y+.5;
        const double zmax=((s_gpu.format>>4)&15u)==1u?65535.:16777215.;
        for(uint32_t i=0;i+2<nindices;i+=3){
            const float *p[3];int valid=1;
            for(unsigned j=0;j<3;j++){
                if(indices[i+j]>=nverts){valid=0;break;}
                p[j]=oracle[indices[i+j]].pos;
                for(unsigned k=0;k<4;k++)if(!isfinite(p[j][k]))valid=0;
                if(p[j][3]<=0)valid=0;
            }
            if(!valid)continue;
            double den=((double)p[1][1]-p[2][1])*((double)p[0][0]-p[2][0])
                      +((double)p[2][0]-p[1][0])*((double)p[0][1]-p[2][1]);
            if(fabs(den)<1e-12)continue;
            if(RS(0x308)){
                int front=RS(0x3A0)==0x900?den>0:den<0;
                if(RS(0x39C)==0x408||(RS(0x39C)==0x405&&!front)
                   ||(RS(0x39C)==0x404&&front))continue;
            }
            double a=(((double)p[1][1]-p[2][1])*(px-p[2][0])
                     +((double)p[2][0]-p[1][0])*(py-p[2][1]))/den;
            double b=(((double)p[2][1]-p[0][1])*(px-p[2][0])
                     +((double)p[0][0]-p[2][0])*(py-p[2][1]))/den;
            double c=1.-a-b,z=a*p[0][2]+b*p[1][2]+c*p[2][2];
            if(a<0||b<0||c<0||z<0||z>zmax)continue;
            if(first_triangle==UINT32_MAX)first_triangle=i/3;
            point_hits++;
        }
        if(!cpu_ok||!point_hits)goto done;
    }else{
    {float x0=oracle[0].pos[0],x1=x0,y0=oracle[0].pos[1],y1=y0;
     for(uint32_t i=0;i<nverts;i++){
         float x=oracle[i].pos[0],y=oracle[i].pos[1];
         if(!isfinite(x)||!isfinite(y))goto done;
         if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;
     }
     if(!cpu_ok||point_x<x0||point_x>x1||point_y<y0||point_y>y1
        ||x1-x0>2500||y1-y0>800)goto done;
    }
    }
    capture_flip=s_gpu.flips;seen[count++]=t->offset;
    ok&=stadium_capture_file(base,"program.u32",program,NV2A_VSH_SLOTS*sizeof *program);
    ok&=stadium_capture_file(base,"constants.f32",constants,NV2A_VSH_CONSTANTS*4*sizeof(float));
    ok&=stadium_capture_file(base,"vertices.f32",vertices,vertex_bytes);
    ok&=stadium_capture_file(base,"indices.u16",indices,(size_t)nindices*sizeof *indices);
    ok&=stadium_capture_file(base,"combiner.bin",&s_gpu.rc,sizeof s_gpu.rc);
    ok&=stadium_capture_file(base,"render-state.u32",s_rs,sizeof s_rs);
    ok&=stadium_capture_file(base,"texture-registers.u32",s_tex_reg,sizeof s_tex_reg);
    ok&=stadium_capture_file(base,"texture-registers-known.u8",s_tex_set,sizeof s_tex_set);
    ok&=stadium_capture_file(base,"cpu-outputs.f32",oracle,(size_t)nverts*sizeof *oracle);
    {uint32_t emitted_used=0;int bytes=nv2a_vsh_emit_hlsl(hlsl,96*1024,&emitted_used);
     if(bytes>0&&emitted_used==used)ok&=stadium_capture_file(base,"vertex.hlsl",hlsl,(size_t)bytes);else ok=0;}
#define STADIUM_META(...) do { if(meta_ok) { int written=snprintf(metadata+meta_used,sizeof metadata-meta_used,__VA_ARGS__); if(written<0 || (size_t)written>=sizeof metadata-meta_used)meta_ok=0; else meta_used+=(size_t)written; } } while(0)
    {
        STADIUM_META("stadium_capture_version=1\nauthor=djultra64\nflip=%u\ndraw=%u\nused=%08X\nnverts=%u\nnindices=%u\nvertex_stride_floats=%u\nprogram_hash=%08X\nprogram_start=%u\nprogram_revision=%u\nconstant_revision=%u\ncpu_oracle_ok=%d\n",
          s_gpu.flips,s_gpu.draws,used,nverts,nindices,per*4,nv2a_vsh_program_hash(),start,revision,version,cpu_ok);
        STADIUM_META("surface=%08X zeta=%08X format=%08X pitch=%u/%u clip=%u,%u,%u,%u\nxf_mode=%08X\ncombiner_control=%08X stage_program=%08X final=%08X/%08X\ndot_mapping=%08X other_stage_input=%08X control_words_known=%04X\n",
          s_gpu.color_offset,s_gpu.zeta_offset,s_gpu.format,s_gpu.pitch,s_gpu.zeta_pitch,s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.xf_mode,
          s_gpu.rc.control,s_gpu.rc.stage_program,s_gpu.rc.final0,s_gpu.rc.final1,s_cmb_c[13],s_cmb_c[14],s_cmb_c_known);
        STADIUM_META("alpha=%u func=%X ref=%u blend=%u func=%X/%X eq=%X constant=%08X color_mask=%08X depth=%u func=%X write=%u fog=%u mode=%X color=%08X params=%a,%a\n",
          s_gpu.alpha_test,s_gpu.alpha_func,s_gpu.alpha_ref,s_gpu.blend_enable,s_gpu.blend_sfactor,s_gpu.blend_dfactor,s_gpu.blend_equation,s_gpu.blend_color,s_gpu.color_mask,s_gpu.depth_test,s_gpu.depth_func,s_gpu.depth_mask,s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_color,(double)s_gpu.fog_param[0],(double)s_gpu.fog_param[1]);
        STADIUM_META("point_mode=%d point_hits=%u first_triangle=%u sample_center=%g,%g\n",point_mode,point_hits,first_triangle,(double)point_x+(point_mode?.5:0),(double)point_y+(point_mode?.5:0));
        STADIUM_META("cull=%u face=%X front=%X primitive=%u raw_indices=%u point=%g,%g\n",RS(0x308),RS(0x39C),RS(0x3A0),s_gpu.prim,s_gpu.idx_count,(double)point_x,(double)point_y);
        for(unsigned a=0;a<4;a++){const Texture*q=&s_gpu.texs[a];
            uint32_t mode=(s_gpu.rc.stage_program>>(a*5))&31u;
            uint32_t stride=q->valid&&mode?tex_face_stride(q):0;
            uint64_t bytes=(uint64_t)stride*(q->cube?6u:1u);
            if(q->valid&&bytes){
                char suffix[32];snprintf(suffix,sizeof suffix,"texture%u.raw",a);
                if(q->offset<0x80000000u||bytes>s_stadium_capture_budget
                   ||(uint64_t)q->offset+bytes>0x84000000ull)ok=0;
                else ok&=stadium_capture_file(base,suffix,(const uint8_t*)xbox_GetMemoryOffset()+q->offset,(size_t)bytes);
            }
            STADIUM_META("texture%u_raw_bytes=%llu face_stride=%u layout=guest_native_including_mips\n",a,(unsigned long long)bytes,stride);
            STADIUM_META("texture%u=%08X size=%ux%u pitch=%u format=%02X filter=%08X address=%X/%X cube=%d levels=%u valid=%d control=%08X key=%08X key_known=%u\n",a,q->offset,q->width,q->height,q->pitch,q->color,q->filter,q->addr_u,q->addr_v,q->cube,q->levels,q->valid,s_gpu.texture_control[a],s_gpu.texture_key[a],s_gpu.texture_key_known[a]);}
        for(unsigned a=0;a<8;a++)STADIUM_META("combiner%u=%08X/%08X outputs=%08X/%08X constants=%08X/%08X\n",a,s_gpu.rc.color_icw[a],s_gpu.rc.alpha_icw[a],s_gpu.rc.color_ocw[a],s_gpu.rc.alpha_ocw[a],s_gpu.rc.factor0[a],s_gpu.rc.factor1[a]);
        STADIUM_META("files_ok=%d\npayload_budget_global=%zu\n",ok,s_stadium_capture_budget);if(meta_ok)ok&=stadium_capture_file(base,"state.txt",metadata,meta_used);else ok=0;
    }
#undef STADIUM_META
    fprintf(stderr,"[STADIUM-CAPTURE] %s flip=%u draw=%u texture=%08X vertices=%u indices=%u files=%s oracle=%d\n",base,s_gpu.flips,s_gpu.draws,t->offset,nverts,nindices,ok?"ok":"FAILED",cpu_ok);
done:
    free(hlsl);free(oracle);
}

/* One-flip late-effect capture. Read-only; no rasterizer substitution.
 * Own work: djultra64. Off unless RECOMP_POINT_CAPTURE_PREFIX is set. */
typedef struct {
    int initialized,enabled,done,active;const char*prefix;
    uint32_t min_flip,flip;double x,y;
    uint64_t draws,hits,manifest_dropped,unresolved[8],payload_failed;
    unsigned manifest_count;char manifest[PB_POINT_CAPTURE_MANIFEST_LIMIT][768];
    PbPointCaptureRing ring;
    uint64_t budget_dropped;
} PbDrawCapture;
static PbDrawCapture s_point_capture,s_hair_capture;
static PbHairSchedule s_hair_schedule;
static size_t s_hair_total_bytes;
static uint32_t s_hair_completed_flip;
static int s_hair_completed_pending;
static int point_capture_on(void)
{
    if(!s_point_capture.initialized){
        const char*e;char*end;unsigned long v;
        s_point_capture.initialized=1;s_point_capture.ring.limit_bytes=6u*1024u*1024u;s_point_capture.min_flip=1000;s_point_capture.x=615;s_point_capture.y=50;
        s_point_capture.prefix=getenv("RECOMP_POINT_CAPTURE_PREFIX");
        s_point_capture.enabled=s_point_capture.prefix&&*s_point_capture.prefix;
        if(!s_point_capture.enabled)return 0;
        e=getenv("RECOMP_POINT_CAPTURE_MIN_FLIP");if(e&&*e){v=strtoul(e,&end,10);if(!*end&&v<=UINT32_MAX)s_point_capture.min_flip=(uint32_t)v;}
        e=getenv("RECOMP_POINT_CAPTURE_POINT");if(e){double x,y;char tail;if(sscanf(e,"%lf,%lf%c",&x,&y,&tail)==2&&isfinite(x)&&isfinite(y)&&x>=0&&y>=0&&x<16384&&y<16384){s_point_capture.x=x;s_point_capture.y=y;}}
    }
    return s_point_capture.enabled&&!s_point_capture.done;
}
static void draw_capture_finish(PbDrawCapture *capture,int hair)
{
    if(!capture->active)return;
    char frame_prefix[1024];const char*prefix=capture->prefix;
    if(hair){int n=snprintf(frame_prefix,sizeof frame_prefix,"%s-frame%u",prefix,capture->flip);if(n<0||(size_t)n>=sizeof frame_prefix){pb_point_ring_clear(&capture->ring);capture->active=0;capture->done=1;return;}prefix=frame_prefix;}
    char path[1024];FILE*f;unsigned files=0,errors=0;
    for(unsigned i=0;i<capture->ring.count;i++){
        const PbPointCaptureSlot*slot=pb_point_ring_at(&capture->ring,i);
        size_t at=0,bytes;const char*name;const unsigned char*data;
        while(pb_point_file_next(slot->data,slot->bytes,&at,&name,&data,&bytes)){
            if(snprintf(path,sizeof path,"%s-%u-%s",prefix,i,name)>=(int)sizeof path){errors++;continue;}
            f=fopen(path,"wb");if(!f){errors++;continue;}
            int ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=0;
            if(ok)files++;else errors++;
        }
        if(at!=slot->bytes)errors++;
    }
    if(snprintf(path,sizeof path,"%s-manifest.txt",prefix)<(int)sizeof path&&(f=fopen(path,"wb"))){
        fprintf(f,"author=djultra64 flip=%u sample_center=%g,%g draws=%llu hits=%llu manifest_dropped=%llu payload_failed=%llu retained=%u retained_bytes=%zu evicted=%llu oversize=%llu allocation_failed=%llu invalid=%llu files=%u errors=%u budget_dropped=%llu total_payload_bytes=%zu capture_kind=%s\n",
            capture->flip,capture->x+.5,capture->y+.5,(unsigned long long)capture->draws,(unsigned long long)capture->hits,
            (unsigned long long)capture->manifest_dropped,(unsigned long long)capture->payload_failed,capture->ring.count,capture->ring.bytes,
            (unsigned long long)capture->ring.evicted,(unsigned long long)capture->ring.oversize,(unsigned long long)capture->ring.allocation_failed,(unsigned long long)capture->ring.invalid,files,errors,(unsigned long long)capture->budget_dropped,s_hair_total_bytes,hair?"hair":"point");
        static const char*reason[8]={"geometry_limit","allocation","oracle_unavailable","primitive","index","nonfinite_or_W_crossing","guest_texture_range","reserved"};
        for(unsigned i=0;i<8;i++)fprintf(f,"unresolved_%s=%llu\n",reason[i],(unsigned long long)capture->unresolved[i]);
        for(unsigned i=0;i<capture->manifest_count;i++)fprintf(f,"%s\n",capture->manifest[i]);
        for(unsigned i=0;i<capture->ring.count;i++){const PbPointCaptureSlot*slot=pb_point_ring_at(&capture->ring,i);fprintf(f,"retained_slot=%u hit=%llu bytes=%zu\n",i,(unsigned long long)slot->sequence,slot->bytes);}
        if(ferror(f))errors++;if(fclose(f))errors++;
    }else errors++;
    fprintf(stderr,"[%s-CAPTURE] prefix=%s flip=%u hits=%llu retained=%u overwritten=%llu manifest_dropped=%llu errors=%u\n",hair?"HAIR":"POINT",prefix,capture->flip,(unsigned long long)capture->hits,capture->ring.count,(unsigned long long)capture->ring.evicted,(unsigned long long)capture->manifest_dropped,errors);
    if(hair){
        s_hair_completed_flip=capture->flip;
        s_hair_completed_pending=capture->ring.count>0 && errors==0;
        pb_hair_schedule_complete(&s_hair_schedule);
    }
    pb_point_ring_clear(&capture->ring);
    if(hair){const char*saved=capture->prefix;memset(capture,0,sizeof *capture);capture->initialized=1;capture->enabled=1;capture->prefix=saved;capture->ring.limit_bytes=PB_HAIR_CAPTURE_FRAME_BYTES;capture->done=s_hair_schedule.completed>=PB_HAIR_CAPTURE_FRAMES;}
    else capture->done=1;
}
static void draw_capture_payload(PbDrawCapture*capture,int hair,const float*vertices,uint32_t nverts,uint32_t used,
                                  const uint16_t*indices,uint32_t nindices,
                                  const PgraphReadyVertex*ready,uint32_t count,int program_mode,int rhw_rebase_exponent)
{
    if(!hair&&(!point_capture_on()||s_gpu.flips<capture->min_flip))return;
    int gpu=vertices!=NULL;const char*path=gpu?"gpu":program_mode?"cpu_vsh":"fixed";
    if(!s_gpu.color_offset||(s_gpu.format&255u)!=0x28u)return;
    if(!capture->active){capture->active=1;capture->flip=s_gpu.flips;}
    if(s_gpu.flips!=capture->flip)return;
    capture->draws++;
    if(hair&&capture->ring.count>=PB_HAIR_CAPTURE_DRAWS){capture->budget_dropped++;return;}
    const uint32_t vertex_limit=hair?8192u:4096u,index_limit=hair?24576u:12288u;
    if((gpu&&(!nverts||nverts>vertex_limit||!nindices||nindices>index_limit))||(!gpu&&(!count||count>vertex_limit))){capture->unresolved[0]++;return;}
    PbPointCaptureState state={s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,
        ((s_gpu.format>>4)&15u)==1u?65535.:16777215.,RS(0x308),RS(0x3A0),RS(0x39C)};
    double px=capture->x+.5,py=capture->y+.5;
    if(!hair&&(px<state.clip_x||py<state.clip_y||px>=state.clip_x+state.clip_width||py>=state.clip_y+state.clip_height))return;
    uint32_t nv=gpu?nverts:count,ni=gpu?nindices:0,per=0,first=UINT32_MAX,hits=0;
    uint16_t *tri=NULL;Nv2aVshOutput*oracle=NULL;float(*positions)[4]=NULL;
    float(*raw)[16][4]=NULL;char metadata[8192];size_t meta_bytes=0;uint32_t revision=0,start=0,version=0;
    const Nv2aVshInstruction*program=NULL;const float*constants=NULL;
    for(unsigned a=0;a<16;a++)per+=(used>>a)&1u;if(!per)per=1;
    positions=malloc((size_t)nv*sizeof *positions);if(!positions){capture->unresolved[1]++;goto done;}
    if(program_mode){program=nv2a_vsh_program_data(&revision,&start);constants=nv2a_vsh_constants(&version);}
    if(gpu){
        int writes=0;nv2a_vsh_inputs_used(&writes);if(writes){capture->unresolved[2]++;goto done;}
        oracle=calloc(nv,sizeof *oracle);if(!oracle){capture->unresolved[1]++;goto done;}
        program=nv2a_vsh_program_data(&revision,&start);constants=nv2a_vsh_constants(&version);
        for(uint32_t i=0;i<nv;i++){
            float in[16][4]={{0}};unsigned off=0;
            for(unsigned a=0;a<16;a++){in[a][3]=1;if((used>>a)&1u){memcpy(in[a],vertices+(size_t)i*per*4+off,16);off+=4;}}
            if(!nv2a_vsh_run((const float(*)[4])in,&oracle[i])){capture->unresolved[2]++;goto done;}
            memcpy(positions[i],oracle[i].pos,16);
        }
    }else{
        for(uint32_t i=0;i<nv;i++){positions[i][0]=ready[i].x;positions[i][1]=ready[i].y;positions[i][2]=ready[i].z;positions[i][3]=ready[i].w;}
        tri=malloc((size_t)nv*3*sizeof *tri);if(!tri){capture->unresolved[1]++;goto done;}
#define POINT_TRI(a,b,c) do{tri[ni++]=(uint16_t)(a);tri[ni++]=(uint16_t)(b);tri[ni++]=(uint16_t)(c);}while(0)
        switch(s_gpu.prim){
        case 5:for(uint32_t i=0;i+2<nv;i+=3)POINT_TRI(i,i+1,i+2);break;
        case 6:for(uint32_t i=0;i+2<nv;i++)POINT_TRI(i,i+1+(i&1u),i+2-(i&1u));break;
        case 7:case 10:for(uint32_t i=1;i+1<nv;i++)POINT_TRI(0,i,i+1);break;
        case 8:for(uint32_t i=0;i+3<nv;i+=4){POINT_TRI(i,i+1,i+2);POINT_TRI(i,i+2,i+3);}break;
        case 9:for(uint32_t i=0;i+3<nv;i+=2){POINT_TRI(i,i+1,i+3);POINT_TRI(i,i+3,i+2);}break;
        default:capture->unresolved[3]++;goto done;
        }
#undef POINT_TRI
        indices=tri;
    }
    double bbox[4]={1e30,-1e30,1e30,-1e30},first_z=0;
    for(uint32_t i=0;i<nv;i++){if(isfinite(positions[i][0])){if(positions[i][0]<bbox[0])bbox[0]=positions[i][0];if(positions[i][0]>bbox[1])bbox[1]=positions[i][0];}if(isfinite(positions[i][1])){if(positions[i][1]<bbox[2])bbox[2]=positions[i][1];if(positions[i][1]>bbox[3])bbox[3]=positions[i][1];}}
    if(!hair)for(uint32_t i=0;i+2<ni;i+=3){
        float p[3][4];int valid=1;for(unsigned j=0;j<3;j++){if(indices[i+j]>=nv){valid=0;break;}memcpy(p[j],positions[indices[i+j]],16);}
        if(!valid){capture->unresolved[4]++;continue;}
        /* draw_ready clamps vertex depth before interpolation; mirror that
         * coverage contract without changing the captured raw positions. */
        if(!gpu)for(unsigned j=0;j<3;j++)if(isfinite(p[j][2]))p[j][2]=p[j][2]<0?0:p[j][2]>state.zmax?(float)state.zmax:p[j][2];
        double z;PbPointCaptureResult result=pb_point_triangle((const float(*)[4])p,px,py,&state,&z);
        if(result==PB_POINT_W_CROSSING||result==PB_POINT_NONFINITE)capture->unresolved[5]++;
        if(result==PB_POINT_HIT){if(first==UINT32_MAX){first=i/3;first_z=z;}hits++;}
    }
    if(hair){hits=1;first=UINT32_MAX;}
    if(!hits)goto done;
    uint64_t hit=++capture->hits;
    char*line=capture->manifest_count<PB_POINT_CAPTURE_MANIFEST_LIMIT?capture->manifest[capture->manifest_count++]:NULL;
    if(line)snprintf(line,768,"hit=%llu draw=%u history_sequence=%llu path=%s primitive=%u vertices=%u indices=%u triangles_at_sample=%u first_triangle=%u surface=%08X format=%08X shader=%05X t0=%08X blend=%u/%X/%X depth=%u/%X/%u cull=%u/%X/%X bbox=%g,%g,%g,%g sample_z=%g",
        (unsigned long long)hit,s_gpu.draws,(unsigned long long)s_history_sequence,path,s_gpu.prim,nv,ni,hits,first,s_gpu.color_offset,s_gpu.format,s_gpu.rc.stage_program,s_gpu.texs[0].offset,s_gpu.blend_enable,s_gpu.blend_sfactor,s_gpu.blend_dfactor,s_gpu.depth_test,s_gpu.depth_func,s_gpu.depth_mask,RS(0x308),RS(0x39C),RS(0x3A0),bbox[0],bbox[1],bbox[2],bbox[3],first_z);
    else capture->manifest_dropped++;
    if(!gpu){raw=calloc(nv,sizeof *raw);if(!raw){capture->payload_failed++;goto done;}}
    if(!gpu)for(uint32_t i=0;i<nv;i++)for(unsigned a=0;a<16;a++){
        if(!fetch_attr(&s_gpu.attr[a],s_gpu.idx[i],raw[i][a]))nv2a_vsh_current((int)a,raw[i][a]);
    }
    int written=snprintf(metadata,sizeof metadata,"point_capture_version=2\nready_stride_bytes=32\nauthor=djultra64\nhit=%llu\nflip=%u\ndraw=%u\npath=%s\nprimitive=%u\nnverts=%u\nnindices=%u\nused=%08X\nvertex_stride_floats=%u\npoint_hits=%u first_triangle=%u sample_center=%g,%g\nsurface=%08X zeta=%08X format=%08X pitches=%u/%u clip=%u,%u,%u,%u\nxf_mode=%08X fixed_written=%04X\nprogram_hash=%08X program_start=%u program_revision=%u constant_revision=%u\ntexture_bytes_source=guest_CPU_not_live_GPU\npacked_rhw_rebase_exponent=%d\n",
        (unsigned long long)hit,s_gpu.flips,s_gpu.draws,path,s_gpu.prim,nv,ni,used,per*4,hits,first,px,py,s_gpu.color_offset,s_gpu.zeta_offset,s_gpu.format,s_gpu.pitch,s_gpu.zeta_pitch,s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.xf_mode,s_gpu.fixed_composite_written,program_mode?nv2a_vsh_program_hash():0,start,revision,version,rhw_rebase_exponent);
    if(written<0||(size_t)written>=sizeof metadata){capture->payload_failed++;goto done;}meta_bytes=(size_t)written;
    written=snprintf(metadata+meta_bytes,sizeof metadata-meta_bytes,"fog_enable=%u fog_mode=%X fog_color_abgr=%08X fog_bias=%a fog_multiplier=%a fog_gen=%u fog_plane=%a,%a,%a,%a\n",s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_color,(double)s_gpu.fog_param[0],(double)s_gpu.fog_param[1],s_gpu.fog_gen,(double)s_gpu.fog_plane[0],(double)s_gpu.fog_plane[1],(double)s_gpu.fog_plane[2],(double)s_gpu.fog_plane[3]);
    if(written<0||(size_t)written>=sizeof metadata-meta_bytes){capture->payload_failed++;goto done;}meta_bytes+=(size_t)written;
    if(line){size_t len=strlen(line);snprintf(line+len,768-len," fog=%u/%X/%08X/%a/%a/gen%u",s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_color,(double)s_gpu.fog_param[0],(double)s_gpu.fog_param[1],s_gpu.fog_gen);}
    written=snprintf(metadata+meta_bytes,sizeof metadata-meta_bytes,"capture_kind=%s point_coverage_evaluated=%u\nalpha_test=%u func=%X ref=%u blend=%u factors=%X/%X eq=%X depth=%u func=%X write=%u stencil=%u func=%X cull=%u face=%X front=%X\n",hair?"hair_draw":"point",!hair,s_gpu.alpha_test,s_gpu.alpha_func,s_gpu.alpha_ref,s_gpu.blend_enable,s_gpu.blend_sfactor,s_gpu.blend_dfactor,s_gpu.blend_equation,s_gpu.depth_test,s_gpu.depth_func,s_gpu.depth_mask,RS(0x32C),RS(0x364),RS(0x308),RS(0x39C),RS(0x3A0));
    if(written<0||(size_t)written>=sizeof metadata-meta_bytes){capture->payload_failed++;goto done;}meta_bytes+=(size_t)written;
    for(unsigned a=0;a<4;a++){const Texture*t=&s_gpu.texs[a];uint32_t ctl=s_gpu.texture_control[a];
        written=snprintf(metadata+meta_bytes,sizeof metadata-meta_bytes,"texture%u=%08X size=%ux%u format=%02X levels=%u filter=%08X control=%08X min_lod=%u max_lod=%u valid=%d disabled=%d address=%X/%X cube=%d raw_bytes=%llu\n",a,t->offset,t->width,t->height,t->color,t->levels,t->filter,ctl,(ctl>>18)&0xfffu,(ctl>>6)&0xfffu,t->valid,t->disabled,t->addr_u,t->addr_v,t->cube,(unsigned long long)(t->valid?(uint64_t)tex_face_stride(t)*(t->cube?6:1):0));
        if(written<0||(size_t)written>=sizeof metadata-meta_bytes){capture->payload_failed++;goto done;}meta_bytes+=(size_t)written;
    }
    struct PointFile{const char*name;const void*data;size_t bytes;} files[24];unsigned nf=0;
#define POINT_FILE(n,p,b) do{files[nf++]=(struct PointFile){n,p,b};}while(0)
    POINT_FILE("state.txt",metadata,meta_bytes);POINT_FILE("indices.u16",indices,(size_t)ni*2);
    POINT_FILE("positions.f32",positions,(size_t)nv*sizeof *positions);
    POINT_FILE("combiner.bin",&s_gpu.rc,sizeof s_gpu.rc);POINT_FILE("render-state.u32",s_rs,sizeof s_rs);
    POINT_FILE("texture-registers.u32",s_tex_reg,sizeof s_tex_reg);POINT_FILE("texture-registers-known.u8",s_tex_set,sizeof s_tex_set);
    POINT_FILE("textures.bin",s_gpu.texs,sizeof s_gpu.texs);POINT_FILE("attributes.bin",s_gpu.attr,sizeof s_gpu.attr);
    POINT_FILE("texture-control.u32",s_gpu.texture_control,sizeof s_gpu.texture_control);
    POINT_FILE("texture-key.u32",s_gpu.texture_key,sizeof s_gpu.texture_key);
    POINT_FILE("texture-key-known.u8",s_gpu.texture_key_known,sizeof s_gpu.texture_key_known);
    POINT_FILE("combiner-control.u32",s_cmb_c,sizeof s_cmb_c);
    POINT_FILE("fixed-composite.f32",s_gpu.fixed_composite,sizeof s_gpu.fixed_composite);
    if(program_mode){POINT_FILE("program.u32",program,NV2A_VSH_SLOTS*sizeof *program);POINT_FILE("constants.f32",constants,NV2A_VSH_CONSTANTS*16);}
    if(gpu){POINT_FILE("vertices.f32",vertices,(size_t)nv*per*16);POINT_FILE("cpu-outputs.f32",oracle,(size_t)nv*sizeof *oracle);}
    else{POINT_FILE("ready.bin",ready,(size_t)nv*sizeof *ready);POINT_FILE("raw-attributes.f32",raw,(size_t)nv*sizeof *raw);}
    char suffix[4][32];
    for(unsigned a=0;a<4;a++){
        const Texture*t=&s_gpu.texs[a];uint64_t bytes=t->valid?(uint64_t)tex_face_stride(t)*(t->cube?6:1):0;
        if(!bytes)continue;
        if(bytes>PB_POINT_CAPTURE_BUDGET||!((t->offset>=0x10000u&&(uint64_t)t->offset+bytes<=0x4000000ull)||(t->offset>=0x80000000u&&(uint64_t)t->offset+bytes<=0x84000000ull))){capture->unresolved[6]++;continue;}
        snprintf(suffix[a],sizeof suffix[a],"texture%u.raw",a);POINT_FILE(suffix[a],(const unsigned char*)xbox_GetMemoryOffset()+t->offset,(size_t)bytes);
    }
#undef POINT_FILE
    size_t total=0;for(unsigned i=0;i<nf;i++){size_t n=pb_point_file_size(files[i].name,files[i].bytes);if(!n||n>PB_POINT_CAPTURE_BUDGET-total){total=PB_POINT_CAPTURE_BUDGET+1u;break;}total+=n;}
    if(hair&&!pb_hair_payload_fits(capture->ring.bytes,capture->ring.count,total,s_hair_total_bytes)){capture->budget_dropped++;goto done;}
    unsigned char*blob=pb_point_ring_begin(&capture->ring,total);
    if(!blob){capture->payload_failed++;goto done;}
    size_t at=0;int ok=1;for(unsigned i=0;i<nf;i++)if(!pb_point_file_append(blob,total,&at,files[i].name,files[i].data,files[i].bytes))ok=0;
    if(!ok||at!=total){pb_point_ring_cancel(&capture->ring);capture->payload_failed++;}else {pb_point_ring_commit(&capture->ring,hit);if(hair)s_hair_total_bytes+=total;}
done:
    free(raw);free(positions);free(oracle);free(tri);
}

static int hair_capture_on(void)
{
    PbDrawCapture*c=&s_hair_capture;
    if(!c->initialized){
        c->initialized=1;c->prefix=getenv("RECOMP_HAIR_CAPTURE_PREFIX");
        c->enabled=c->prefix&&*c->prefix;c->ring.limit_bytes=PB_HAIR_CAPTURE_FRAME_BYTES;
        s_hair_schedule.min_flip=1000;
        if(c->enabled){const char*e=getenv("RECOMP_HAIR_CAPTURE_MIN_FLIP");char*end;unsigned long n;
            if(e&&*e){n=strtoul(e,&end,10);if(!*end&&n<=UINT32_MAX)s_hair_schedule.min_flip=(uint32_t)n;}}
    }
    return c->enabled&&!c->done;
}
/* Pure metadata query in the synchronous post-Present callback. The executor
 * may increment FLIP_WRITE before STALL; return the payload flip explicitly,
 * without assuming it equals the host's frame number. */
int nv2a_pb_hair_capture_completed_frame(uint32_t *gpu_flip)
{
    if(!s_hair_completed_pending)return 0;
    if(gpu_flip)*gpu_flip=s_hair_completed_flip;
    return 1;
}
static void hair_capture_payload(const float*vertices,uint32_t nverts,uint32_t used,
    const uint16_t*indices,uint32_t nindices,const PgraphReadyVertex*ready,
    uint32_t count,int program_mode,int rhw_rebase_exponent)
{
    if(!hair_capture_on()||!program_mode)return;
    const Texture*t=&s_gpu.texs[0];
    PbHairDrawState d={nv2a_vsh_program_hash(),s_gpu.color_offset,s_gpu.format,s_gpu.pitch,
        s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.alpha_test,s_gpu.alpha_func,s_gpu.alpha_ref,
        (uint32_t)(t->valid&&!t->disabled),t->color,t->width,t->height};
    if(!pb_hair_draw_match(&d))return;
    if(!s_hair_capture.active&&!pb_hair_schedule_due(&s_hair_schedule,s_gpu.flips))return;
    draw_capture_payload(&s_hair_capture,1,vertices,nverts,used,indices,nindices,ready,count,program_mode,rhw_rebase_exponent);
}

/* Keep a depth-only shadow pass away from the main depth attachment. Surface
 * selection occurs after register decoding and before clears AND draws.
 * The AA0 linear RGBA copy surface is supported; AA1 main rendering keeps
 * the existing baseline path unless the separate AA1 experiment is enabled.
 * Own integration: djultra64. */
static int route_guest_surface(void)
{
#ifdef XBOXRECOMP_VISUAL_EXPORT
    vsj_backend_boundary(VSJ_SCENE_ENTER,s_gpu.flips,0);
#endif
    static int enabled=-1,rgba_enabled=-1,aa1_enabled=-1;
    if(enabled<0) enabled=getenv("RECOMP_GUEST_TARGETS")!=NULL;
    if(rgba_enabled<0) {
        const char*e=getenv("RECOMP_RGBA_TARGETS");
        rgba_enabled=e && !strcmp(e,"1");
    }
    if(aa1_enabled<0) {
        const char*e=getenv("RECOMP_GUEST_AA1");
        aa1_enabled=e && !strcmp(e,"1");
    }
    if(!enabled) return 1;
    PgraphRenderSurface surface;
    const PgraphRenderSurface *selected=NULL;
    /* XDK clears RGB565 in pitched view (0113), then renders the same
     * allocation in swizzled view (0213). Both must select its attachments;
     * 0113/color=0 remains the independent shadow-depth destination. */
    if(((s_gpu.format&0xffffu)==0x0113u)
       || ((s_gpu.format&0xffffu)==0x0213u && s_gpu.color_offset)
       || (rgba_enabled && s_gpu.color_offset
           && ((s_gpu.format&0xffffu)==0x0128u
               || (s_gpu.format&0xffffu)==0x0228u
               || (aa1_enabled && (s_gpu.format&0xffffu)==0x1128u)))) {
        surface=(PgraphRenderSurface){s_gpu.color_offset?dma_resolve(s_gpu.color_offset):0,
            s_gpu.zeta_offset?dma_resolve(s_gpu.zeta_offset):0,
            s_gpu.clip_x+s_gpu.clip_w,s_gpu.clip_y+s_gpu.clip_h,
            s_gpu.pitch,s_gpu.zeta_pitch,s_gpu.format,
            s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,1};
        selected=&surface;
    }
    int result=pgraph_d3d11_select_surface(selected);
    if(result!=1) {
        static unsigned reported;
        if(reported++<8)
            fprintf(stderr,"[SURFACE] selection failed (%d) fmt=%08X color=%08X zeta=%08X clip=%ux%u+%u+%u pitch=%u/%u resolved=%08X/%08X; refusing main fallback\n",
                result,s_gpu.format,s_gpu.color_offset,s_gpu.zeta_offset,
                s_gpu.clip_w,s_gpu.clip_h,s_gpu.clip_x,s_gpu.clip_y,s_gpu.pitch,s_gpu.zeta_pitch,
                s_gpu.color_offset?dma_resolve(s_gpu.color_offset):0,
                s_gpu.zeta_offset?dma_resolve(s_gpu.zeta_offset):0);
        return 0;
    }
    if(selected) {
        static uint32_t seen[32];static unsigned count;
        unsigned i;uint32_t key=surface.color?surface.color:surface.zeta;
        for(i=0;i<count && seen[i]!=key;i++);
        if(i==count && count<32) {
            seen[count++]=key;
            fprintf(stderr,"[SURFACE] isolated %s color=%08X zeta=%08X %ux%u pitch=%u/%u fmt=%08X\n",
                !surface.color?"shadow":((surface.format&15u)==8?"RGBA":"RGB565"),surface.color,surface.zeta,surface.width,surface.height,
                surface.color_pitch,surface.zeta_pitch,surface.format);
        }
    }
    return 1;
}

/* Opt-in movie clipping diagnostic. No guest reads, allocations or renderer
 * mutations. Deduplicate shapes/state (not double-buffer addresses) so intro
 * frames cannot consume the entire preview budget. Own work: djultra64. */
#define TOPSPIN_MOVIE_TRACE_DIAGNOSTIC 1
static void movie_trace_ready(const PgraphReadyVertex *v, uint32_t n)
{
    static int enabled=-1;
    typedef struct {uint32_t shape[16],rs[64];float bounds[8],matrix[16];} Tuple;
    static Tuple seen[16];static unsigned shown;
    Tuple t;unsigned i,j;const Texture *tex=&s_gpu.texs[0];
    if(enabled<0){const char*e=getenv("TOPSPIN_MOVIE_TRACE");enabled=e&&!strcmp(e,"1");}
    if(!enabled||shown==16||!n||!v||!tex->valid||(tex->color!=0x24&&tex->color!=0x25))return;
    memset(&t,0,sizeof t);
    uint32_t shape[16]={s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.format,s_gpu.pitch,s_gpu.zeta_pitch,
        tex->color,tex->width,tex->height,tex->pitch,tex->addr_u,tex->addr_v,s_gpu.xf_mode,s_gpu.prim,n};
    memcpy(t.shape,shape,sizeof shape);memcpy(t.rs,s_rs,sizeof t.rs);memcpy(t.matrix,s_gpu.fixed_composite,sizeof t.matrix);
    unsigned finite=0;
    for(i=0;i<n;i++){
        const float a[4]={v[i].x,v[i].y,v[i].u,v[i].v};
        if(!isfinite(a[0])||!isfinite(a[1])||!isfinite(a[2])||!isfinite(a[3]))continue;
        for(j=0;j<4;j++){if(!finite||a[j]<t.bounds[j])t.bounds[j]=a[j];if(!finite||a[j]>t.bounds[j+4])t.bounds[j+4]=a[j];}finite++;
    }
    for(i=0;i<shown;i++)if(!memcmp(&seen[i],&t,sizeof t))return;
    seen[shown++]=t;
    const float *scale=nv2a_vsh_constant(58),*offset=nv2a_vsh_constant(59);
    fprintf(stderr,"[MOVIE-PB] tuple=%u flip=%u draw=%u prim=%u vertices=%u finite=%u rt=%08X zeta=%08X fmt=%08X pitch=%u/%u clip=%ux%u+%u+%u bbox=%g,%g..%g,%g uv=%g,%g..%g,%g tex=%08X fmt=%02X size=%ux%u pitch=%u row=%u addr=%u/%u xf=%u matrixKnown=%04X shader=%08X control=%08X key=%08X\n",
        shown,s_gpu.flips,s_gpu.draws,s_gpu.prim,n,finite,s_gpu.color_offset,s_gpu.zeta_offset,s_gpu.format,s_gpu.pitch,s_gpu.zeta_pitch,
        s_gpu.clip_w,s_gpu.clip_h,s_gpu.clip_x,s_gpu.clip_y,t.bounds[0],t.bounds[1],t.bounds[4],t.bounds[5],t.bounds[2],t.bounds[3],t.bounds[6],t.bounds[7],
        tex->offset,tex->color,tex->width,tex->height,tex->pitch,linear_row_texels(tex),tex->addr_u,tex->addr_v,s_gpu.xf_mode,s_gpu.fixed_composite_written,
        s_gpu.rc.stage_program,s_gpu.texture_control[0],s_gpu.texture_key[0]);
    fprintf(stderr,"  movie-pb viewport scale=%g,%g,%g,%g offset=%g,%g,%g,%g rs300..3fc=",scale[0],scale[1],scale[2],scale[3],offset[0],offset[1],offset[2],offset[3]);
    for(i=0;i<64;i++)fprintf(stderr,"%s%08X",i?",":"",t.rs[i]);
    fprintf(stderr," matrix=");for(i=0;i<16;i++)fprintf(stderr,"%s%g",i?",":"",t.matrix[i]);
    fprintf(stderr,"\n");
    for(i=0;i<n&&i<4;i++)fprintf(stderr,"  movie-pb v%u xyzw=%g,%g,%g,%g uv=%g,%g rawUVderived=%g,%g color=%08X\n",i,v[i].x,v[i].y,v[i].z,v[i].w,v[i].u,v[i].v,
        v[i].u*linear_row_texels(tex),v[i].v*tex->height,v[i].color);
}

static void draw_primitive(void)
{
    float v[4];
    uint32_t i;

    if (!s_gpu.prim || !s_gpu.idx_count)
        return;
    int history_routed=route_guest_surface();
    history_record(PB_HISTORY_DRAW,NV097_SET_BEGIN_END,s_gpu.prim,history_routed);
    if (!history_routed)
        return;
    /* Texture color keys precede dependent texture stages, independently of
     * alpha testing and blending. Never invent an unobserved key value. */
    for(unsigned stage=0;stage<4;stage++)
        pgraph_d3d11_set_texture_control_state(stage,s_gpu.texture_control[stage],
            s_gpu.texture_key[stage],(int)s_gpu.texture_key_known[stage]);
    /* Forward the title's raw NV097 bias in its zeta units. The host chooses
     * its depth representation after target selection and converts units. */
    float polygon_factor,polygon_units;
    uint32_t factor_bits=RS(0x384),unit_bits=RS(0x388);
    memcpy(&polygon_factor,&factor_bits,sizeof polygon_factor);
    memcpy(&polygon_units,&unit_bits,sizeof polygon_units);
    pgraph_d3d11_set_polygon_offset_state(RS(0x338)!=0,polygon_factor,polygon_units);
    pgraph_d3d11_set_fog(s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_color,s_gpu.fog_param[0],s_gpu.fog_param[1]);
    s_texture_alias_failed=0;
    s_gpu.draws++;
    if ((s_gpu.draws % 200) == 0 && !runtime_quiet_enabled())
        fprintf(stderr, "  [GPU] draw #%u\n", s_gpu.draws);
    s_gpu.verts += s_gpu.idx_count;

    /* How many batches carry coordinates at all, and what range they span.
     * A pipeline that decodes perfectly and draws nothing is indistinguishable
     * from one that never ran, unless the vertices themselves are measured. */
    {
        float p[4];
        if (fetch_attr(&s_gpu.attr[0], s_gpu.idx[0], p)) {
            if (p[0] != 0.0f || p[1] != 0.0f || p[2] != 0.0f) {
                s_gpu.nonzero_draws++;
                if (p[0] < s_gpu.min_x) s_gpu.min_x = p[0];
                if (p[0] > s_gpu.max_x) s_gpu.max_x = p[0];
                if (p[1] < s_gpu.min_y) s_gpu.min_y = p[1];
                if (p[1] > s_gpu.max_y) s_gpu.max_y = p[1];
            }
        }
    }

    {
        uint32_t t0 = s_gpu.tris_drawn;
        uint64_t p0 = s_gpu.pixels;
        raster_batch();
        if (s_ftrace == 2) {
            fprintf(stderr, "[FTRACE] %s prim %u n %u surf %08X %ux%u+%u+%u"
                    " pitch %u | tex %s %08X %ux%u fmt %02X | blend %u %X/%X"
                    " eq %X | z test %u func %X mask %u | tris %u px %llu\n",
                    (s_gpu.xf_mode & 3) == 2 ? "PRG" : "FIX", s_gpu.prim,
                    s_gpu.idx_count, s_gpu.color_offset, s_gpu.clip_w,
                    s_gpu.clip_h, s_gpu.clip_x, s_gpu.clip_y, s_gpu.pitch,
                    s_gpu.texs[0].valid ? "on" : "off", s_gpu.texs[0].offset,
                    s_gpu.texs[0].width, s_gpu.texs[0].height, s_gpu.texs[0].color,
                    s_gpu.blend_enable, s_gpu.blend_sfactor,
                    s_gpu.blend_dfactor, s_gpu.blend_equation,
                    s_gpu.depth_test, s_gpu.depth_func, s_gpu.depth_mask,
                    s_gpu.tris_drawn - t0,
                    (unsigned long long)(s_gpu.pixels - p0));
            if ((s_gpu.xf_mode & 3) == 2)
                fprintf(stderr, "[FTRACE]     v0 %g %g %g %g  v1 %g %g  v2 %g %g%c",
                        s_xf[0].pos[0], s_xf[0].pos[1], s_xf[0].pos[2],
                        s_xf[0].pos[3], s_xf[1].pos[0], s_xf[1].pos[1],
                        s_xf[2].pos[0], s_xf[2].pos[1], 10);
            if (s_gpu.rc_seen) {
                int st;
                fprintf(stderr, "[FTRACE]     rc stages %u ctl %08X prog %05X fin %08X %08X",
                        s_gpu.rc.control & 0xFF, s_gpu.rc.control,
                        s_gpu.rc.stage_program, s_gpu.rc.final0, s_gpu.rc.final1);
                for (st = 0; st < 4; st++)
                    if ((s_gpu.rc.stage_program >> (st * 5)) & 0x1F)
                        fprintf(stderr, " | t%d %08X %ux%u fmt %02X%s", st,
                                s_gpu.texs[st].offset, s_gpu.texs[st].width,
                                s_gpu.texs[st].height, s_gpu.texs[st].color,
                                s_gpu.texs[st].valid ? "" : " (invalid)");
                fputc(10, stderr);
                for (st = 0; st < (int)(s_gpu.rc.control & 0xFF) && st < 8; st++)
                    fprintf(stderr, "[FTRACE]       s%d icw %08X %08X ocw %08X %08X c %08X %08X%c",
                            st, s_gpu.rc.color_icw[st], s_gpu.rc.alpha_icw[st],
                            s_gpu.rc.color_ocw[st], s_gpu.rc.alpha_ocw[st],
                            s_gpu.rc.factor0[st], s_gpu.rc.factor1[st], 10);
            }
            if ((s_gpu.xf_mode & 3) == 2) {
                uint32_t r;
                for (r = 112; r < 116; r++) {
                    const float *c = nv2a_vsh_constant(r);
                    fprintf(stderr, "[FTRACE]     c[%u] %g %g %g %g%c", r,
                            c[0], c[1], c[2], c[3], 10);
                }
            }
        }
    }

    /* Hand the same batch to the D3D11 translator, fetched and in screen
     * space.
     *
     * The translator's own vertex path only understands data carried inline in
     * the command stream. This title, like most, points the GPU at attribute
     * arrays in memory and draws by index, and resolving those addresses is
     * this file's job -- it already tracks the DMA objects they live in and
     * has fetch_attr() for reading them. So the fetch happens here and the
     * translator is handed vertices it can draw directly.
     *
     * Attribute slots are the NV2A's fixed assignment: 0 position, 3 diffuse,
     * 9 texture coordinate zero.
     *
     * Only batches already in screen space are forwarded, for the same reason
     * raster_batch() only draws those: a title running a vertex program hands
     * over object-space positions, and without executing the program they mean
     * nothing. Those are left for the day the translator runs vertex programs,
     * rather than drawn somewhere wrong. */
    {
        static int fwd = -1;
        if (fwd < 0)
            fwd = getenv("RECOMP_PB_D3D11") != NULL;
        if (fwd && s_gpu.prim && s_gpu.idx_count >= 3
                && s_gpu.attr[0].size && s_gpu.attr[0].stride) {
            static PgraphReadyVertex ready[NV_MAX_INDICES];
            static int no_vsh = -1;
            uint32_t n = 0, i;
            int vsh;
            if (no_vsh < 0)
                no_vsh = getenv("RECOMP_NO_VSH") != NULL;
            /* In program mode the positions are whatever the title's vertex
             * program expects, not pixels, so run it (nv2a_vsh_cpu.c). */
            vsh = !no_vsh && nv2a_vsh_active();
            /* The guest already folds viewport transforms into oPos. Its
             * surface clip constrains both vertex paths without shrinking
             * the allocation or host viewport. Own integration: djultra64. */
            pgraph_d3d11_set_surface_clip(s_gpu.clip_x, s_gpu.clip_y, s_gpu.clip_w, s_gpu.clip_h);
            {
                /* The batch dump's frame: trace this batch's first vertex
                 * through the program and name its attribute streams. */
                static long dump = -2;
                if (dump == -2) {
                    dump = 0;
                }
                if (batch_dump_flip(s_gpu.flips)) {
                    uint32_t a;
                    fprintf(stderr, "  [BATCH] attrs:");
                    for (a = 0; a < NV_VERTEX_ATTRS; a++)
                        if (s_gpu.attr[a].size)
                            fprintf(stderr, " v%u t%u s%u st%u", a, s_gpu.attr[a].type,
                                    s_gpu.attr[a].size, s_gpu.attr[a].stride);
                    fprintf(stderr, " vsh %d\n", vsh);
                    if (vsh)
                        nv2a_vsh_trace(1);
                }
            }
            /* A vertex an indexed batch names again comes out the same, so
             * each index runs the program once per batch (vc_*): a fight's
             * meshes name most vertices several times. Not when the program
             * writes a constant (one vertex's result feeds the next) or a
             * trace is on. RECOMP_VSH_NO_REUSE=1 turns it off. Only the
             * inputs the program reads are fetched. */
            enum { VC_SIZE = 1024 };
            static uint32_t vc_tag[VC_SIZE], vc_gen[VC_SIZE], vc_cur;
            static PgraphReadyVertex vc_val[VC_SIZE];
            static int no_reuse = -1;
            uint32_t used = 0;
            int reuse = 0;
            if (no_reuse < 0)
                no_reuse = getenv("RECOMP_VSH_NO_REUSE") != NULL;
            if (vsh) {
                int wc = 0;
                used = nv2a_vsh_inputs_used(&wc);
                reuse = !no_reuse && !wc && !batch_dump_flip(s_gpu.flips);
                if (++vc_cur == 0) {
                    memset(vc_gen, 0, sizeof(vc_gen));
                    vc_cur = 1;
                }
            }
            /* The program runs on the GPU (M4e; RECOMP_VSH_GPU=0 for the CPU). The
             * batch's distinct vertices go across as one float4 per input the
             * program reads, with a triangle list into them; the CPU loop
             * below is skipped. Not for a batch-dump frame (its trace is the
             * CPU interpreter's), a program that writes constants, points or
             * lines, or a program the GPU side has refused. */
            static float    gv[NV_MAX_INDICES * 16 * 4];
            static uint16_t gtri[NV_MAX_INDICES * 3];
            uint32_t gnv = 0, gnt = 0;
            float gsu = 1.0f, gsv = 1.0f;
            int gpu = 0;
            {
                static int gpu_on = -1;
                int wc2 = 1;
                if (gpu_on < 0) {
                    const char *e = getenv("RECOMP_VSH_GPU");
                    gpu_on = !(e && *e == '0');
                }
                if (vsh && gpu_on)
                    (void)nv2a_vsh_inputs_used(&wc2);
                gpu = vsh && gpu_on && !wc2
                    && ((s_gpu.prim >= 5 && s_gpu.prim <= 10) || s_gpu.prim == 1)
                    && !batch_dump_flip(s_gpu.flips) && pgraph_d3d11_program_usable();
            }
            if (gpu) {
                static uint32_t gm_tag[VC_SIZE], gm_gen[VC_SIZE], gm_cur;
                static uint16_t gm_val[VC_SIZE];
                static uint16_t lidx[NV_MAX_INDICES];
                uint32_t k, cnt = s_gpu.idx_count < NV_MAX_INDICES ? s_gpu.idx_count : NV_MAX_INDICES;
                /* Input layout is constant for this draw. Write the upload's
                 * packed stride directly, avoiding a second full-vertex copy. */
                uint32_t per = 0;
                for (k = 0; k < 16; k++)
                    per += (used >> k) & 1;
                if (!per)
                    per = 1;
                if (++gm_cur == 0) {
                    memset(gm_gen, 0, sizeof(gm_gen));
                    gm_cur = 1;
                }
                for (k = 0; k < cnt; k++) {
                    uint32_t vi = s_gpu.idx[k], slot = vi & (VC_SIZE - 1), a;
                    if (gm_gen[slot] == gm_cur && gm_tag[slot] == vi) {
                        lidx[k] = gm_val[slot];
                        continue;
                    }
                    {
                        float *dst = gv + (size_t)gnv * per * 4;
                        uint32_t o = 0;
                        for (a = 0; a < 16; a++) {
                            if (!((used >> a) & 1))
                                continue;
                            if (!fetch_attr(&s_gpu.attr[a], vi, dst + o))
                                nv2a_vsh_current((int)a, dst + o);
                            o += 4;
                        }
                        if (!used)
                            dst[0] = dst[1] = dst[2] = 0.0f, dst[3] = 1.0f;
                    }
                    gm_tag[slot] = vi;
                    gm_gen[slot] = gm_cur;
                    gm_val[slot] = (uint16_t)gnv;
                    lidx[k] = (uint16_t)gnv++;
                }
                /* Preserve primitive facing while converting to a triangle list.
                 * Top Spin enables culling on stadium/body materials. */
                switch (s_gpu.prim) {
                case 1:                     /* points: one index each (point sprites) */
                    for (k = 0; k < cnt; k++)
                        gtri[gnt++] = lidx[k];
                    break;
                case 5:
                    for (k = 0; k + 2 < cnt; k += 3) {
                        gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 1]; gtri[gnt++] = lidx[k + 2];
                    }
                    break;
                case 6:
                    for (k = 0; k + 2 < cnt; k++) {
                        gtri[gnt++] = lidx[k];
                        gtri[gnt++] = lidx[k + 1 + (k & 1u)];
                        gtri[gnt++] = lidx[k + 2 - (k & 1u)];
                    }
                    break;
                case 7: case 10:
                    for (k = 1; k + 1 < cnt; k++) {
                        gtri[gnt++] = lidx[0]; gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 1];
                    }
                    break;
                case 8:
                    for (k = 0; k + 3 < cnt; k += 4) {
                        gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 1]; gtri[gnt++] = lidx[k + 2];
                        gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 2]; gtri[gnt++] = lidx[k + 3];
                    }
                    break;
                case 9:
                    for (k = 0; k + 3 < cnt; k += 2) {
                        gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 1]; gtri[gnt++] = lidx[k + 3];
                        gtri[gnt++] = lidx[k]; gtri[gnt++] = lidx[k + 3]; gtri[gnt++] = lidx[k + 2];
                    }
                    break;
                default:
                    break;
                }
                if (s_gpu.texs[0].valid && !tex_size_from_format(s_gpu.texs[0].color)
                        && s_gpu.texs[0].width && s_gpu.texs[0].height) {
                    gsu = 1.0f / (float)linear_row_texels(&s_gpu.texs[0]);
                    gsv = 1.0f / (float)s_gpu.texs[0].height;
                }
                /* gates the texture and skip logic below */
                n = (gnt >= 3 || (s_gpu.prim == 1 && gnt >= 1)) ? 3 : 0;
            }
            for (i = 0; !gpu && i < s_gpu.idx_count && n < NV_MAX_INDICES; i++) {
                uint32_t vi = s_gpu.idx[i];
                float pos[4], uv[4];
                if (vsh) {
                    float in[16][4];
                    NvVshOutput o;
                    uint32_t a, slot = vi & (VC_SIZE - 1);
                    if (reuse && vc_gen[slot] == vc_cur && vc_tag[slot] == vi) {
                        ready[n++] = vc_val[slot];
                        continue;
                    }
                    for (a = 0; a < 16; a++) {
                        if (!((used >> a) & 1)) {
                            in[a][0] = in[a][1] = in[a][2] = 0.0f;
                            in[a][3] = 1.0f;
                        } else if (!fetch_attr(&s_gpu.attr[a], vi, in[a])) {
                            nv2a_vsh_current((int)a, in[a]);
                        }
                    }
                    if (!nv2a_vsh_run_ready((const float (*)[4])in, &o))
                        break;
                    uv[0] = o.t0[0];
                    uv[1] = o.t0[1];
                    if (s_gpu.texs[0].valid && !tex_size_from_format(s_gpu.texs[0].color)
                            && s_gpu.texs[0].width && s_gpu.texs[0].height) {
                        uv[0] /= (float)linear_row_texels(&s_gpu.texs[0]);
                        uv[1] /= (float)s_gpu.texs[0].height;
                    }
                    ready[n].x = o.pos[0];
                    ready[n].y = o.pos[1];
                    ready[n].z = o.pos[2];
                    ready[n].w = o.pos[3];
                    ready[n].fog = d3d8_nv_fog_factor(o.fog,s_gpu.fog_enable,s_gpu.fog_mode,s_gpu.fog_param[0],s_gpu.fog_param[1]);
                    ready[n].u = uv[0];
                    ready[n].v = uv[1];
                    {
                        float c[4];
                        int k;
                        for (k = 0; k < 4; k++)
                            c[k] = o.d0[k] < 0.0f ? 0.0f : o.d0[k] > 1.0f ? 1.0f : o.d0[k];
                        ready[n].color = ((uint32_t)(c[3] * 255.0f) << 24)
                                       | ((uint32_t)(c[0] * 255.0f) << 16)
                                       | ((uint32_t)(c[1] * 255.0f) <<  8)
                                       |  (uint32_t)(c[2] * 255.0f);
                    }
                    if (reuse) {
                        vc_tag[slot] = vi;
                        vc_gen[slot] = vc_cur;
                        vc_val[slot] = ready[n];
                    }
                    n++;
                    continue;
                }
                if (!fetch_attr(&s_gpu.attr[0], vi, pos))
                    break;
                /* texcoord_attr(), not slot 9: this title puts position,
                 * texture coordinates and colour in slots 0, 1 and 2 rather
                 * than the NV2A convention, and that helper already falls back
                 * to the format, where a float2 is a texture coordinate and
                 * nothing else. Reading slot 9 found nothing and every quad
                 * drew untextured. */
                if (!fetch_attr(texcoord_attr(), vi, uv))
                    uv[0] = uv[1] = 0.0f;
                /* A linear texture is addressed in texels and Direct3D 11
                 * wants [0,1] (fetch_texcoord has the same rule the other way
                 * round). Passed through as they were, a 640x480 movie frame's
                 * coordinates all clamped to its edge and the whole screen was
                 * one texel's colour. */
                if (s_gpu.texs[0].valid && !tex_size_from_format(s_gpu.texs[0].color)
                        && s_gpu.texs[0].width && s_gpu.texs[0].height) {
                    uv[0] /= (float)linear_row_texels(&s_gpu.texs[0]);
                    uv[1] /= (float)s_gpu.texs[0].height;
                }
                fixed_position_ready(pos, &ready[n]);
                ready[n].fog = fixed_fog_factor(vi,pos);
                ready[n].u     = uv[0];
                ready[n].v     = uv[1];
                ready[n].color = vertex_color(vi);
                n++;
            }
            if (n >= 3) {
                /* Bind whatever texture this batch was programmed with before
                 * drawing it. The address is already resolved through the DMA
                 * object, so what goes across is a host pointer the translator
                 * can copy from. */
                bind_extra_stages(gpu);
                pgraph_d3d11_set_combiner_state(&s_gpu.rc);
                if (s_gpu.texs[0].valid) {
                    bind_texture_ready(&s_gpu.texs[0]);
                    pgraph_d3d11_set_texture_address(s_gpu.texs[0].addr_u, s_gpu.texs[0].addr_v);
                    pgraph_d3d11_set_texture_filter(s_tex_set[5] ? s_tex_reg[5] : 0);
                } else {
                    pgraph_d3d11_set_texture_ready(0, NULL, 0, 0, 0);
                }
                /* RECOMP_PB_BATCH_DUMP=<flip>: one line per batch of that
                 * frame -- what it samples and the extent of what it sends.
                 * The verbose print shows the first six batches of the run,
                 * which says nothing about a screen minutes in. */
                {
                    static long dump = -2;
                    if (dump == -2) {
                        dump = 0;
                    }
                    if (batch_dump_flip(s_gpu.flips)) {
                        float x0 = ready[0].x, x1 = x0, y0 = ready[0].y, y1 = y0;
                        float u0 = ready[0].u, u1 = u0, v0 = ready[0].v, v1 = v0;
                        float z0 = ready[0].z, z1 = z0;
                        uint32_t k;
                        for (k = 1; k < n; k++) {
                            if (ready[k].z < z0) z0 = ready[k].z;
                            if (ready[k].z > z1) z1 = ready[k].z;
                            if (ready[k].x < x0) x0 = ready[k].x;
                            if (ready[k].x > x1) x1 = ready[k].x;
                            if (ready[k].y < y0) y0 = ready[k].y;
                            if (ready[k].y > y1) y1 = ready[k].y;
                            if (ready[k].u < u0) u0 = ready[k].u;
                            if (ready[k].u > u1) u1 = ready[k].u;
                            if (ready[k].v < v0) v0 = ready[k].v;
                            if (ready[k].v > v1) v1 = ready[k].v;
                        }
                        fprintf(stderr, "  [BATCH] f%u prim %u n %u tex %08X %ux%u p%u fmt %02X v%d"
                                        " x %.1f..%.1f y %.1f..%.1f u %.3f..%.3f v %.3f..%.3f"
                                        " col %08X\n",
                                s_gpu.flips, s_gpu.prim, n, s_gpu.texs[0].offset,
                                s_gpu.texs[0].width, s_gpu.texs[0].height, s_gpu.texs[0].pitch,
                                s_gpu.texs[0].color, s_gpu.texs[0].valid, x0, x1, y0, y1,
                                u0, u1, v0, v1, ready[0].color);
                        fprintf(stderr, "  [BATCH]   z %g..%g surface fmt %08X\n",
                                z0, z1, s_gpu.format);
                        {
                            /* RECOMP_PB_PROBE=x,y[;x,y...]: every triangle of this
                             * batch that covers one of those pixels (up to 8),
                             * with its depth there -- the layers at a point, in
                             * draw order. Triangles with a vertex behind the
                             * camera are left out (their screen positions mean
                             * nothing). */
                            static int probes = -1;
                            static float px[8], py[8];
                            int q;
                            if (probes < 0) {
                                const char *e = getenv("RECOMP_PB_PROBE");
                                probes = 0;
                                while (e && probes < 8 && sscanf(e, "%f,%f", &px[probes], &py[probes]) == 2) {
                                    probes++;
                                    e = strchr(e, ';');
                                    if (e) e++;
                                }
                            }
                            if (probes && n >= 3 && s_gpu.prim >= 5 && s_gpu.prim <= 8) {
                                uint32_t step = (s_gpu.prim == 5) ? 3 : (s_gpu.prim == 8) ? 4 : 1;
                                for (k = 0; k + 2 < n; k += step) {
                                    uint32_t t, nt = (s_gpu.prim == 8 && k + 3 < n) ? 2 : 1;
                                    for (t = 0; t < nt; t++) {
                                        const PgraphReadyVertex *a = &ready[s_gpu.prim == 7 ? 0 : k];
                                        const PgraphReadyVertex *b = &ready[k + 1 + t];
                                        const PgraphReadyVertex *c = &ready[k + 2 + t];
                                        float d = (b->y - c->y) * (a->x - c->x) + (c->x - b->x) * (a->y - c->y);
                                        if (a->w <= 0 || b->w <= 0 || c->w <= 0 || d == 0)
                                            continue;
                                        for (q = 0; q < probes; q++) {
                                            float l0 = ((b->y - c->y) * (px[q] - c->x) + (c->x - b->x) * (py[q] - c->y)) / d;
                                            float l1 = ((c->y - a->y) * (px[q] - c->x) + (a->x - c->x) * (py[q] - c->y)) / d;
                                            float l2 = 1.0f - l0 - l1;
                                            if (l0 < 0 || l1 < 0 || l2 < 0)
                                                continue;
                                            fprintf(stderr, "  [PROBE] f%u at %.0f,%.0f tex %08X tri %u z %.2f"
                                                            " (w %.3f %.3f %.3f) zw %u blend %u stencil %u\n",
                                                    s_gpu.flips, px[q], py[q], s_gpu.texs[0].offset, k,
                                                    (double)(l0 * a->z + l1 * b->z + l2 * c->z),
                                                    a->w, b->w, c->w, RS(0x35C), RS(0x304), RS(0x32C));
                                        }
                                    }
                                }
                            }
                        }
                        fprintf(stderr, "  [BATCH]   cmb ctl %08X stage %08X/%08X f0 %08X %08X f1 %08X %08X"
                                        " cicw %08X %08X cocw %08X %08X aicw %08X %08X aocw %08X %08X"
                                        " spfog %08X %08X%c",
                                s_cmb_c[8], s_cmb_c[12], s_cmb_c[14],
                                s_cmb_b[0], s_cmb_b[1], s_cmb_b[8], s_cmb_b[9],
                                s_cmb_b[24], s_cmb_b[25], s_cmb_c[0], s_cmb_c[1],
                                s_cmb_a[0], s_cmb_a[1], s_cmb_b[16], s_cmb_b[17],
                                s_cmb_a[10], s_cmb_a[11], 10);
                        {
                            int st;
                            for (st = 1; st < 4; st++) {
                                uint32_t k = (uint32_t)st * 16;
                                if (s_tex_set[k] && s_tex_set[k + 3] && (s_tex_reg[k + 3] & (1u << 30)))
                                    fprintf(stderr, "  [BATCH]   stage %d tex %08X format %08X address %08X"
                                                    " ctl0 %08X ctl1 %08X rect %08X palette %08X (%s)\n",
                                            st, dma_resolve(s_tex_reg[k]), s_tex_reg[k + 1],
                                            s_tex_reg[k + 2], s_tex_reg[k + 3], s_tex_reg[k + 4],
                                            s_tex_reg[k + 7], s_tex_reg[k + 8],
                                            s_tex_set[k + 8] ? "set" : "never set");
                            }
                        }
                        fprintf(stderr, "  [BATCH]   rs atest %u %X/%02X blend %u %X/%X eq %X"
                                        " cmask %08X depth %u %X zw %u stencil %u func %X"
                                        " ref %X rmask %X wmask %X ops %X/%X/%X"
                                        " cull %u %X front %X offset %u %08X %08X"
                                        " blendcolor %08X%s\n",
                                RS(0x300), RS(0x33C), RS(0x340), RS(0x304), RS(0x344),
                                RS(0x348), RS(0x350), RS(0x358), RS(0x30C), RS(0x354),
                                RS(0x35C), RS(0x32C), RS(0x364), RS(0x368), RS(0x36C),
                                RS(0x360), RS(0x370), RS(0x374), RS(0x378),
                                RS(0x308), RS(0x39C), RS(0x3A0),
                                RS(0x338), RS(0x384), RS(0x388), RS(0x34C),
                                s_draw_arrays ? " arrays" : "");
                    }
                }
                /* RECOMP_SKIP_RT_SAMPLE=1: leave out a batch that samples a
                 * surface the title rendered into (surf_holds_texture). Its
                 * contents are already in the back buffer here, where every
                 * surface is drawn; the copy would paint them over black. */
                {
                    static int skip_rt = -1;
                    if (skip_rt < 0)
                        skip_rt = getenv("RECOMP_SKIP_RT_SAMPLE") != NULL;
                    if (skip_rt && s_gpu.texs[0].valid && surf_holds_texture(s_gpu.texs[0].offset)) {
                        static unsigned skipped;
                        if ((++skipped & (skipped - 1)) == 0)
                            fprintf(stderr, "  [SURF] skipped a batch sampling render target 0x%08X"
                                            " (#%u)%c", s_gpu.texs[0].offset, skipped, 10);
                        n = 0;
                    }
                }
                /* RECOMP_PB_SKIP_TEX=0xA,0xB: leave out batches that sample
                 * those textures -- to find which pass spoils a picture. */
                {
                    static uint32_t skip_tex[8];
                    static int skip_n = -1;
                    int k;
                    if (skip_n < 0) {
                        const char *e = getenv("RECOMP_PB_SKIP_TEX");
                        skip_n = 0;
                        while (e && *e && skip_n < 8) {
                            char *end;
                            skip_tex[skip_n++] = (uint32_t)strtoul(e, &end, 0);
                            e = (*end == ',') ? end + 1 : "";
                        }
                    }
                    for (k = 0; k < skip_n; k++)
                        if (s_gpu.texs[0].valid && s_gpu.texs[0].offset == skip_tex[k])
                            n = 0;
                }
                {
                    /* RECOMP_TEX_ONLY=1 (experiment): white diffuse, so a batch
                     * shows its texture alone. */
                    static int tex_only = -1;
                    uint32_t k;
                    if (tex_only < 0)
                        tex_only = getenv("RECOMP_TEX_ONLY") != NULL;
                    if (tex_only)
                        for (k = 0; k < n; k++)
                            ready[k].color |= 0x00FFFFFFu;
                }
                if (n >= 3)
                    ws_place_batch(vsh, used, gpu ? NULL : ready, n);
                if(s_texture_alias_failed) return;
                if (n >= 3 && gpu) {
                    /* Uniform pre-RCP homogeneous rebasing only on the
                     * owned packed copy. Capture that same GPU input truth. */
                    int rhw_rebase_exponent = 0;
                    if (s_gpu.prim != 1)
                        nv2a_vsh_rebase_rhw_batch(gv, gnv, used,
                            ((s_gpu.format >> 4) & 15u) == 1u
                                ? 1.0f / 65535.0f : 1.0f / 16777215.0f,
                            &rhw_rebase_exponent);
                    if(point_capture_on())draw_capture_payload(&s_point_capture,0,gv,gnv,used,gtri,gnt,NULL,0,vsh,rhw_rebase_exponent);
                    hair_capture_payload(gv,gnv,used,gtri,gnt,NULL,0,vsh,rhw_rebase_exponent);
                    crowd_capture_payload(gv, gnv, used, gtri, gnt);
                    stadium_capture_payload(gv, gnv, used, gtri, gnt);
                    pgraph_d3d11_set_point_mode(s_gpu.prim == 1, s_point_size);
#ifdef XBOXRECOMP_VISUAL_EXPORT
                    uint64_t visual_ticket = 0;
                    if(pb_visual_interested(nv2a_vsh_program_hash()))
                        visual_ticket=pb_visual_before(nv2a_vsh_program_hash());
#endif
                    (void)pgraph_d3d11_draw_program(gv, gnv, used, gtri, gnt, gsu, gsv);
#ifdef XBOXRECOMP_VISUAL_EXPORT
                    if(visual_ticket||vsj_backend_enabled()){
                        uint32_t visual_revision=0,visual_start=0,visual_constants_version=0;
                        const Nv2aVshInstruction*visual_program=nv2a_vsh_program_data(&visual_revision,&visual_start);
                        const float*visual_constants=nv2a_vsh_constants(&visual_constants_version);
                        VfpSource visual_source={0};
                        visual_source.version=VFP_VERSION;visual_source.primitive=s_gpu.prim==1?1u:5u;
                        visual_source.used=used;visual_source.nvertices=gnv;visual_source.nindices=gnt;
                        visual_source.program_start=visual_start;visual_source.program_hash=nv2a_vsh_program_hash();
                        visual_source.program_revision=visual_revision;visual_source.constant_revision=visual_constants_version;
                        visual_source.original_frame=s_gpu.flips;
                        uint32_t visual_per=0;for(uint32_t a=0;a<16;a++)visual_per+=(used>>a)&1u;
                        if(!visual_per)visual_per=1;
                        visual_source.vertices=gv;visual_source.vertex_bytes=(size_t)gnv*visual_per*4*sizeof(float);
                        visual_source.indices=gtri;visual_source.index_bytes=(size_t)gnt*sizeof(uint16_t);
                        visual_source.program=visual_program?(const uint32_t*)visual_program:NULL;
                        visual_source.program_bytes=136u*4u*sizeof(uint32_t);
                        visual_source.constants=visual_constants;visual_source.constant_bytes=192u*4u*sizeof(float);
                        visual_source.camera.encoding=VFP_CAMERA_BAKED_NV2A;
                        visual_source.camera.viewport[0]=s_gpu.clip_x;visual_source.camera.viewport[1]=s_gpu.clip_y;
                        visual_source.camera.viewport[2]=s_gpu.clip_w;visual_source.camera.viewport[3]=s_gpu.clip_h;
                        memcpy(visual_source.camera.fixed_composite,s_gpu.fixed_composite,sizeof s_gpu.fixed_composite);
                        /* Depth convention and texcoord scale are explicit metadata,
                         * not a fabricated independent camera or simulation tick. */
                        float visual_depth[2]={0.0f,1.0f};
                        memcpy(visual_source.camera.depth_range_bits,visual_depth,sizeof visual_depth);
                        vpli_program_source(&visual_source);
                        if(vsj_backend_enabled())vsj_backend_program_source(&visual_source);
                        if(visual_ticket)pb_visual_record_backend(visual_ticket,&visual_source,gsu,gsv,rhw_rebase_exponent);
                    }
#endif
                    pgraph_d3d11_set_point_mode(0, 0.0f);
                }
                else if (n >= 3) {
                    if(point_capture_on())draw_capture_payload(&s_point_capture,0,NULL,0,used,NULL,0,ready,n,vsh,0);
                    hair_capture_payload(NULL,0,used,NULL,0,ready,n,vsh,0);
                    if(!vsh)movie_trace_ready(ready,n);
                    pgraph_d3d11_draw_ready(s_gpu.prim, ready, n);
                }
            }
            {
                /* Which attribute slots the title actually populated. Texture
                 * coordinates read zero out of slot 9, so either they are in
                 * another slot or they never arrive as an array at all, and
                 * the slot map is what separates those two. */
                static int shown;
                if (shown++ < 2) {
                    uint32_t k;
                    for (k = 0; k < NV_VERTEX_ATTRS; k++)
                        if (s_gpu.attr[k].size || s_gpu.attr[k].stride)
                            fprintf(stderr, "  [GPU] attr %2u: off 0x%08X"
                                    " type %u size %u stride %u\n",
                                    k, s_gpu.attr[k].offset, s_gpu.attr[k].type,
                                    s_gpu.attr[k].size, s_gpu.attr[k].stride);
                    fflush(stderr);
                }
            }
        }
    }

    if (pb_exec_verbose()) {
        static int shown;
        if (shown++ < 6) {
            fprintf(stderr, "  [GPU] prim %u, %u indices, pos attr:"
                            " off 0x%08X type %u size %u stride %u\n",
                    s_gpu.prim, s_gpu.idx_count, s_gpu.attr[0].offset,
                    s_gpu.attr[0].type, s_gpu.attr[0].size, s_gpu.attr[0].stride);
            /* The texture stage, for either kind of batch. This used to print
             * only for inline batches, which meant a title drawing through
             * vertex arrays -- Half-Life 2's menu, for one -- showed no
             * texture state at all, and the reason a quad sampled flat was
             * invisible. */
            fprintf(stderr, "  [GPU]   tex: off 0x%08X %ux%u pitch %u"
                            " colour 0x%02X swizzled %d valid %d\n",
                    s_gpu.texs[0].offset, s_gpu.texs[0].width, s_gpu.texs[0].height,
                    s_gpu.texs[0].pitch, s_gpu.texs[0].color,
                    d3d8_format_is_swizzled(s_gpu.texs[0].color), s_gpu.texs[0].valid);
            {
                uint32_t k;
                for (k = 0; k < s_gpu.idx_count && k < 3; k++) {
                    float t[2];
                    if (fetch_texcoord(s_gpu.idx[k], t))
                        fprintf(stderr, "  [GPU]   uv[%u] = %.3f %.3f\n",
                                k, t[0], t[1]);
                }
            }
            /* An inline batch has no guest buffer to go and look at -- the
             * vertices are the payload -- so print the payload too. */
            if (s_gpu.inline_active) {
                uint32_t k;
                fprintf(stderr, "  [GPU]   inline %u dwords:", s_gpu.inline_count);
                for (k = 0; k < s_gpu.inline_count && k < 16; k++)
                    fprintf(stderr, " %08X", s_gpu.inline_buf[k]);
                fprintf(stderr, "\n");
                for (k = 0; k < s_gpu.idx_count && k < 4; k++) {
                    float t[2];
                    if (fetch_texcoord(s_gpu.idx[k], t))
                        fprintf(stderr, "  [GPU]   uv[%u] = %.3f %.3f\n",
                                k, t[0], t[1]);
                }
                fprintf(stderr, "  [GPU]   tex: off 0x%08X %ux%u pitch %u"
                                " colour 0x%02X valid %d\n",
                        s_gpu.texs[0].offset, s_gpu.texs[0].width, s_gpu.texs[0].height,
                        s_gpu.texs[0].pitch, s_gpu.texs[0].color, s_gpu.texs[0].valid);
            }
            {
                /* Every attribute the batch has, not just position. If the
                 * other streams carry data and position does not, the problem
                 * is one buffer rather than the whole vertex path. */
                const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
                uint32_t a, k;
                for (a = 0; a < NV_VERTEX_ATTRS; a++) {
                    const VertexAttr *at = &s_gpu.attr[a];
                    uint32_t nz = 0;
                    if (!at->offset || !at->size)
                        continue;
                    for (k = 0; k < 64; k++)
                        if (mem[at->offset + k]) nz++;
                    fprintf(stderr, "  [GPU]   attr%-2u off 0x%08X type %u"
                                    " size %u stride %-3u  %u/64 bytes set\n",
                            a, at->offset, at->type, at->size, at->stride, nz);
                }
            }
            {
                /* Raw bytes at the array, in case the values read as zero:
                 * that looks the same whether the offset is wrong or the
                 * buffer genuinely has not been filled yet. */
                const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
                uint32_t k;
                fprintf(stderr, "  [GPU]   bytes @0x%08X:", s_gpu.attr[0].offset);
                for (k = 0; k < 32; k++)
                    fprintf(stderr, " %02X", mem[s_gpu.attr[0].offset + k]);
                fprintf(stderr, "\n");
                fprintf(stderr, "  [GPU]   indices:");
                for (k = 0; k < s_gpu.idx_count && k < 8; k++)
                    fprintf(stderr, " %u", s_gpu.idx[k]);
                fprintf(stderr, "\n");
            }
            for (i = 0; i < s_gpu.idx_count && i < 3; i++) {
                if (fetch_attr(&s_gpu.attr[0], s_gpu.idx[i], v))
                    fprintf(stderr, "  [GPU]   v[%u] = %.3f %.3f %.3f %.3f\n",
                            s_gpu.idx[i], v[0], v[1], v[2], v[3]);
            }
        }
    }
}

/* Draw the vertices the title wrote straight into the pushbuffer.
 *
 * INLINE_ARRAY carries no offsets and no indices: the dwords between BEGIN and
 * END *are* the vertex buffer, packed in attribute order using the same
 * SET_VERTEX_DATA_ARRAY_FORMAT registers an ordinary array would use. So the
 * whole batch is describable as a vertex array whose base happens to be that
 * payload, which means synthesising the layout and handing it to the existing
 * path -- rather than a second copy of the topology and rasterisation code.
 *
 * The title's own attribute table is saved and put back: these offsets and
 * strides are ours, and it has not stopped using its.
 *
 * ponytail: each attribute is padded to a whole dword. That is exact for the
 * float and D3DCOLOR formats every inline batch actually uses; a packed
 * sub-dword attribute would need the unpadded layout.
 */
static void draw_inline_array(void)
{
    VertexAttr saved[NV_VERTEX_ATTRS];
    uint32_t off = 0, a, i, vsize, count;

    memcpy(saved, s_gpu.attr, sizeof saved);

    for (a = 0; a < NV_VERTEX_ATTRS; a++) {
        uint32_t bytes;
        if (!s_gpu.attr[a].size)
            continue;
        switch (s_gpu.attr[a].type) {
        case 0:  bytes = 4;                        break;  /* D3DCOLOR   */
        case 2:  bytes = 4 * s_gpu.attr[a].size;   break;  /* float      */
        case 4:  bytes = s_gpu.attr[a].size;       break;  /* ubyte norm */
        case 1:                                            /* short norm */
        case 5:  bytes = 2 * s_gpu.attr[a].size;   break;  /* short      */
        default: bytes = 4 * s_gpu.attr[a].size;   break;
        }
        s_gpu.attr[a].offset = off;
        off += (bytes + 3u) & ~3u;
    }
    vsize = off;
    if (!vsize)
        goto out;

    count = (s_gpu.inline_count * 4) / vsize;
    if (count < 3 || count > NV_MAX_INDICES)
        goto out;
    for (a = 0; a < NV_VERTEX_ATTRS; a++)
        if (s_gpu.attr[a].size)
            s_gpu.attr[a].stride = vsize;

    for (i = 0; i < count; i++)
        s_gpu.idx[i] = (uint16_t)i;
    s_gpu.idx_count = count;

    s_gpu.inline_active = 1;
    draw_primitive();
    s_gpu.inline_active = 0;

out:
    memcpy(s_gpu.attr, saved, sizeof saved);
    s_gpu.idx_count = 0;
}

/* Draw the vertices SET_VERTEX3F/4F completed.
 *
 * Same trick as draw_inline_array: rather than a second copy of the topology
 * and rasterisation code, describe what was accumulated as an ordinary vertex
 * array and hand it to the existing path. The layout is ours and fixed, so
 * the attribute table is written out here rather than derived from the
 * title's format registers.
 *
 * The title's own table is saved and put back -- it has not stopped using it.
 *
 * ponytail: position, diffuse and texcoord0 only. That is what a 2D quad
 * carries and what this rasteriser samples; a second texcoord set or a normal
 * would need the D3D11 translator, not more slots here.
 */
static void draw_immediate(void)
{
    VertexAttr saved[NV_VERTEX_ATTRS];
    uint32_t i;

    if (s_gpu.imm_count < 3)
        return;

    memcpy(saved, s_gpu.attr, sizeof saved);
    memset(s_gpu.attr, 0, sizeof s_gpu.attr);
    /* Offsets are byte offsets into inline_buf here, not guest addresses --
     * fetch_attr reads them that way while inline_active is set, which is
     * also why 0 is a legal offset for position. Every attribute is a float4;
     * the ones the batch never set carry their standing values, as on the
     * GPU. */
    for (i = 0; i < NV_VERTEX_ATTRS; i++) {
        s_gpu.attr[i].type = 2;
        s_gpu.attr[i].size = 4;
        s_gpu.attr[i].offset = i * 16;
        s_gpu.attr[i].stride = IMM_VERTEX_DWORDS * 4;
    }

    for (i = 0; i < s_gpu.imm_count && i < NV_MAX_INDICES; i++)
        s_gpu.idx[i] = (uint16_t)i;
    s_gpu.idx_count = i;

    /* fetch_attr bounds-checks against inline_count dwords. */
    s_gpu.inline_count = s_gpu.imm_count * IMM_VERTEX_DWORDS;
    s_gpu.inline_active = 1;
    draw_primitive();
    s_gpu.inline_active = 0;

    memcpy(s_gpu.attr, saved, sizeof saved);
    s_gpu.idx_count = 0;
    s_gpu.inline_count = 0;
}

/* A vertex is complete: append all 16 attributes in the layout
 * draw_immediate describes. */
static void imm_emit_vertex(void)
{
    uint32_t at = s_gpu.imm_count * IMM_VERTEX_DWORDS;

    if (!s_gpu.prim || at + IMM_VERTEX_DWORDS > NV_MAX_INLINE)
        return;
    memcpy(&s_gpu.inline_buf[at], s_gpu.imm_attr, sizeof s_gpu.imm_attr);
    s_gpu.imm_count++;
}

/* The immediate-mode writes. Returns 1 if `method` was one of them.
 *
 * Split out because it is a range test against five separate bases, and that
 * reads better than five more cases in an already long switch.
 */
/* The immediate-mode attribute methods (xemu pgraph.c SET_VERTEX_DATA*).
 * Each sets an attribute's current value; completing attribute 0 -- the
 * position -- emits a vertex carrying every attribute as it stands. This used
 * to keep position, diffuse and texcoord 0 only, which dropped any quad with
 * a second texture coordinate: Burnout 3 composites its whole 3D scene into
 * the frame with exactly such a quad, and the frame never got the scene. */
static int imm_vertex_method(uint32_t method, uint32_t param)
{
    union { uint32_t u; float f; } v;
    uint32_t attr, c;
    float *a;
    v.u = param;

    if (method >= NV097_SET_VERTEX4F && method < NV097_SET_VERTEX4F + 16) {
        c = (method - NV097_SET_VERTEX4F) / 4;
        s_gpu.imm_attr[0][c] = v.f;
        s_gpu.imm_used |= 1;
        if (c == 3)
            imm_emit_vertex();
        return 1;
    }
    if (method >= NV097_SET_VERTEX3F && method < NV097_SET_VERTEX3F + 12) {
        c = (method - NV097_SET_VERTEX3F) / 4;
        s_gpu.imm_attr[0][c] = v.f;
        s_gpu.imm_used |= 1;
        if (c == 2) {
            s_gpu.imm_attr[0][3] = 1.0f;
            imm_emit_vertex();
        }
        return 1;
    }
    if (method >= NV097_SET_VERTEX_DATA2F_M
            && method < NV097_SET_VERTEX_DATA2F_M + NV_VERTEX_ATTRS * 8) {
        attr = (method - NV097_SET_VERTEX_DATA2F_M) / 8;
        c = ((method - NV097_SET_VERTEX_DATA2F_M) % 8) / 4;
        a = s_gpu.imm_attr[attr];
        a[c] = v.f;
        if (c == 1) {
            a[2] = 0.0f; a[3] = 1.0f;
        }
        s_gpu.imm_used |= (uint16_t)(1u << attr);
        if (attr == 0 && c == 1)
            imm_emit_vertex();
        return 1;
    }
    if (method >= NV097_SET_VERTEX_DATA4F_M
            && method < NV097_SET_VERTEX_DATA4F_M + NV_VERTEX_ATTRS * 16) {
        attr = (method - NV097_SET_VERTEX_DATA4F_M) / 16;
        c = ((method - NV097_SET_VERTEX_DATA4F_M) % 16) / 4;
        s_gpu.imm_attr[attr][c] = v.f;
        s_gpu.imm_used |= (uint16_t)(1u << attr);
        if (attr == 0 && c == 3)
            imm_emit_vertex();
        return 1;
    }
    if (method >= NV097_SET_VERTEX_DATA2S
            && method < NV097_SET_VERTEX_DATA2S + NV_VERTEX_ATTRS * 4) {
        attr = (method - NV097_SET_VERTEX_DATA2S) / 4;
        a = s_gpu.imm_attr[attr];
        a[0] = (float)(int16_t)(param & 0xFFFF);
        a[1] = (float)(int16_t)(param >> 16);
        a[2] = 0.0f; a[3] = 1.0f;
        s_gpu.imm_used |= (uint16_t)(1u << attr);
        if (attr == 0)
            imm_emit_vertex();
        return 1;
    }
    if (method >= NV097_SET_VERTEX_DATA4UB
            && method < NV097_SET_VERTEX_DATA4UB + NV_VERTEX_ATTRS * 4) {
        /* Bytes in register order, x from the low byte (xemu). */
        attr = (method - NV097_SET_VERTEX_DATA4UB) / 4;
        a = s_gpu.imm_attr[attr];
        a[0] = (float)( param        & 0xFF) / 255.0f;
        a[1] = (float)((param >>  8) & 0xFF) / 255.0f;
        a[2] = (float)((param >> 16) & 0xFF) / 255.0f;
        a[3] = (float)( param >> 24        ) / 255.0f;
        s_gpu.imm_used |= (uint16_t)(1u << attr);
        if (attr == 0)
            imm_emit_vertex();
        return 1;
    }
    if (method >= NV097_SET_VERTEX_DATA4S_M
            && method < NV097_SET_VERTEX_DATA4S_M + NV_VERTEX_ATTRS * 8) {
        attr = (method - NV097_SET_VERTEX_DATA4S_M) / 8;
        c = ((method - NV097_SET_VERTEX_DATA4S_M) % 8) / 4;
        a = s_gpu.imm_attr[attr];
        a[c * 2]     = (float)(int16_t)(param & 0xFFFF);
        a[c * 2 + 1] = (float)(int16_t)(param >> 16);
        s_gpu.imm_used |= (uint16_t)(1u << attr);
        if (attr == 0 && c == 1)
            imm_emit_vertex();
        return 1;
    }
    return 0;
}
/* The texture stage registers, all four stages. Stage i's registers are
 * 0x1B00 + 0x40*i onwards; only stage 0 used to be decoded, which was
 * enough for menus drawn one texture at a time and not for a 3D frame that
 * composites its scene through a second stage. */
static void tex_stage_method(uint32_t method, uint32_t param)
{
    uint32_t stage = (method - NV_TEX_FIRST) / 0x40;
    uint32_t reg = NV_TEX_FIRST + ((method - NV_TEX_FIRST) & 0x3F);
    Texture *t = &s_gpu.texs[stage];

    switch (reg) {
    case NV097_SET_TEXTURE_OFFSET:
        /* A DMA-object offset like a surface's: physical, reached through
         * the contiguous window when it names contiguous memory. */
        t->offset = dma_resolve(param);
        break;
    case NV097_SET_TEXTURE_FORMAT:
        t->color = (param >> 8) & 0xFF;
        t->cube = (param >> 2) & 1;
        t->levels = (param >> 16) & 0xF;
        /* A swizzled texture carries its own dimensions here, as log2 in
         * BASE_SIZE_U/V. It has to: IMAGE_RECT describes a linear image, and
         * a title that only uses swizzled textures never sends one. */
        if (tex_size_from_format(t->color)) {
            t->width  = 1u << ((param >> 20) & 0xF);
            t->height = 1u << ((param >> 24) & 0xF);
        }
        break;
    case NV097_SET_TEXTURE_PALETTE:
        /* The low six bits carry the DMA context and the entry count. */
        t->palette = dma_resolve(param & ~0x3Fu);
        t->palette_len = 256u >> ((param >> 2) & 3);
        break;
    case NV097_SET_TEXTURE_ADDRESS:
        /* Four bits per axis. 1 is wrap, 3 clamp-to-edge; mirror and border
         * fall back to clamp, wrong at an edge rather than everywhere. */
        t->addr_u =  param        & 0xF;
        t->addr_v = (param >>  8) & 0xF;
        break;
    case NV097_SET_TEXTURE_CONTROL0:
        s_gpu.texture_control[stage]=param;
        t->disabled = !(param & (1u << 30));
        break;
    case NV097_SET_TEXTURE_CONTROL1:
        t->pitch = param >> 16;          /* linear formats only */
        break;
    case NV097_SET_TEXTURE_IMAGE_RECT:
        t->width  = param >> 16;
        t->height = param & 0xFFFF;
        break;
    case 0x1B14:                                  /* SET_TEXTURE_FILTER */
        t->filter = param;
        break;
    default:
        break;
    }
    tex_update_valid(t);
    record_tex_reg(method, param);
}

void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param)
{
    static int inited;
    if (!inited) {
        inited = 1;
        s_gpu.color_mask = 0x01010101u;           /* all channels, as reset */
        {
            int i;
            for (i = 0; i < NV_VERTEX_ATTRS; i++)
                s_gpu.imm_attr[i][3] = 1.0f;
            for (i = 0; i < 3; i++)
                s_gpu.imm_attr[3][i] = s_gpu.imm_attr[4][i] = 1.0f;
        }
        s_gpu.min_x = s_gpu.min_y = 1e30f;
        s_gpu.max_x = s_gpu.max_y = -1e30f;
    }
    /* Bring-up: the first parameters each surface method carries. A wrong
     * pitch or clip is indistinguishable from a method never arriving unless
     * the values are visible. */
    if (pb_exec_verbose()) {
        static int shown[8];
        int slot = -1;
        switch (method) {
        case NV097_SET_SURFACE_CLIP_HORIZONTAL: slot = 0; break;
        case NV097_SET_SURFACE_CLIP_VERTICAL:   slot = 1; break;
        case NV097_SET_SURFACE_FORMAT:          slot = 2; break;
        case NV097_SET_SURFACE_PITCH:           slot = 3; break;
        case NV097_SET_SURFACE_COLOR_OFFSET:    slot = 4; break;
        case NV097_SET_COLOR_CLEAR_VALUE:       slot = 5; break;
        case NV097_CLEAR_SURFACE:               slot = 6; break;
        default: break;
        }
        if (slot >= 0 && shown[slot]++ < 4)
            fprintf(stderr, "  [GPU] subch %u method 0x%04X param 0x%08X\n",
                    subch, method, param);
    }

    if (s_ftrace == 2) {
        /* RECOMP_FRAME_TRACE_METHODS: every method of the traced frame,
         * less the bulk (vertex data, program and constant uploads). */
        static int all = -1;
        if (all < 0)
            all = getenv("RECOMP_FRAME_TRACE_METHODS") != NULL;
        if (all && !(method >= 0x1800 && method < 0x1A00)
            && !(method >= 0x0B00 && method < 0x0C00))
            fprintf(stderr, "[FTRACE]   m %u:%04X = %08X%c", subch, method,
                    param, 10);
    }
    /* Hand the same method to the D3D11 translator.
     *
     * This decoder walks the title's real command stream, which is the hard
     * part and is already done; src/nv2a/nv2a_pgraph_d3d11.c turns methods
     * into draw calls but had nothing feeding it, because the model it belongs
     * to does not parse push buffers. Joining the two is the documented
     * upgrade path out of the software rasteriser below, which can only draw
     * flat untextured geometry that is already in screen space.
     *
     * Before the subchannel check on purpose: the translator keeps its own
     * per-subchannel state and should see everything the title submits, not
     * just what this file knows what to do with.
     *
     * Opt-in until it has been proven against more than one title, and it
     * needs a D3D11 device to exist, which the host program creates. With no
     * device the translator does nothing and the software path still runs, so
     * enabling it cannot make things worse than they were. */
    {
        static int fwd = -1;
        if (fwd < 0)
            fwd = getenv("RECOMP_PB_D3D11") != NULL;
        if (fwd) {
            int allowed=1;
            if(subch==0 && method==NV097_CLEAR_SURFACE) {
                allowed=route_guest_surface();
                history_record(PB_HISTORY_CLEAR,method,param,allowed);
            }
            if(allowed)pgraph_d3d11_method((int)subch, method, param);
        }
    }
    if (subch == 0 && fixed_transform_method(method, param))
        return;
    if (subch == 0 && method >= 0x300 && method <= 0x3FC)
        RS(method) = param;
    if (subch == 0) {
        if (method >= 0x260 && method <= 0x28C)
            s_cmb_a[(method - 0x260) / 4] = param;
        else if (method >= 0xA60 && method <= 0xAFC)
            s_cmb_b[(method - 0xA60) / 4] = param;
        else if (method >= 0x1E40 && method <= 0x1E7C) {
            s_cmb_c[(method - 0x1E40) / 4] = param;
            s_cmb_c_known |= (uint16_t)(1u<<((method-0x1E40)/4));
        }
        else if (method == 0x1E20 || method == 0x1E24)
            s_cmb_fc[(method - 0x1E20) / 4] = param;
    }

    /* A NOP with a parameter is a software method: the GPU raises PGRAPH's
     * notify interrupt for it, and Direct3D waits on what its handler does.
     * See kernel_nv2a_software_method. */
    /* A fence, in the two methods Direct3D uses for one (Def Jam:
     * sub_0021EB90): the semaphore release that writes the reference, and
     * the NV04 pattern colour it reads back from PATT_COLOR0. Run in order,
     * so a fence completes when the executor passes it, not when the title
     * submits it. */
    if (subch == 0 && method == 0x1D70)
        xbox_Nv2aSemaphoreRelease(param);
    if (subch == 5 && method == 0x0310)
        xbox_Nv2aRegWrite(0xFD400B10u, param);

    if (subch == 0 && method == 0x043C)
        s_point_size = (float)(param & 0x1FFu) / 8.0f;

    if (method == 0x0100 && param != 0) {
        extern void nv2a_pb_stall(void);
        kernel_nv2a_software_method(subch, method, param);
        nv2a_pb_stall();                   /* and wait for it, as the GPU does */
    }

    if (subch == 0 && method >= NV_TEX_FIRST && method <= NV_TEX_LAST) {
        tex_stage_method(method, param);
        return;
    }
    if (subch != 0) {                      /* 3D class lives on subchannel 0 */
        note_unhandled(method, param);
        return;
    }
    /* Vertex programs, their constants and the transform mode. */
    switch (method) {
    case NV097_SET_SURFACE_CLIP_HORIZONTAL:
        s_gpu.clip_x = param & 0xFFFF;
        s_gpu.clip_w = (param >> 16) & 0xFFFF;
        break;
    case NV097_SET_SURFACE_CLIP_VERTICAL:
        s_gpu.clip_y = param & 0xFFFF;
        s_gpu.clip_h = (param >> 16) & 0xFFFF;
        if (batch_dump_flip(s_gpu.flips))
            fprintf(stderr, "  [BATCH] f%u surface clip %ux%u+%u+%u\n", s_gpu.flips,
                    s_gpu.clip_w, s_gpu.clip_h, s_gpu.clip_x, s_gpu.clip_y);
        break;
    case NV097_SET_SURFACE_FORMAT:
        s_gpu.format = param;
        break;
    case NV097_SET_SURFACE_PITCH:
        s_gpu.pitch = param & 0xFFFF;      /* colour pitch; zeta is the top half */
        s_gpu.zeta_pitch = param >> 16;
        break;
    case NV097_SET_SURFACE_COLOR_OFFSET:
        if (s_ftrace == 2)
            fprintf(stderr, "[FTRACE] color offset -> %08X%c", param, 10);
        s_gpu.color_offset = param;
        if(rjp_enabled()){
            extern volatile uint32_t g_pb_live_va;
            uint32_t resolved=param?dma_resolve(param):0,base,size;uint64_t epoch=0;
            if(resolved)(void)xbox_ContiguousEpoch(resolved,1,&base,&size,&epoch);
            (void)rjp_target(g_pb_live_va,resolved,epoch);
        }
        surf_note_target(dma_resolve(param));
        if (batch_dump_flip(s_gpu.flips))
            fprintf(stderr, "  [BATCH] f%u colour surface -> 0x%08X%c", s_gpu.flips,
                    dma_resolve(param), 10);
        break;
    case NV097_SET_COLOR_CLEAR_VALUE:
        s_gpu.clear_color = param;
        break;
    case NV097_SET_BLEND_ENABLE:
        s_gpu.blend_enable = param;
        break;
    case NV097_SET_BLEND_EQUATION:
        s_gpu.blend_equation = param;
        break;
    case 0x0358:                                  /* SET_COLOR_MASK */
        s_gpu.color_mask = param;
        break;

    /* Register combiners. */
    case 0x0288: s_gpu.rc.final0 = param; break;  /* SPECULAR_FOG_CW0 */
    case 0x028C: s_gpu.rc.final1 = param; break;  /* SPECULAR_FOG_CW1 */
    case 0x1E20: s_gpu.rc.final_c0 = param; break;/* SPECULAR_FOG_FACTOR */
    case 0x1E24: s_gpu.rc.final_c1 = param; break;
    case 0x1E60:                                  /* COMBINER_CONTROL */
        s_gpu.rc.control = param;
        s_gpu.rc_seen = 1;
        break;
    case 0x1E70: s_gpu.rc.stage_program = param; break;
    case 0x17F8: s_gpu.clip_plane_mode = param; break;

    /* Alpha test and fog. */
    case 0x0300: s_gpu.alpha_test = param; break;
    case 0x033C: s_gpu.alpha_func = param; break;
    case 0x0340: s_gpu.alpha_ref = param; break;
    case 0x02A0: s_gpu.fog_gen = param; break;
    case 0x02A4: s_gpu.fog_enable = param; break;
    case 0x02A8: s_gpu.fog_color = param; break;  /* R in bits 0-7 */
    case 0x029C: s_gpu.fog_mode = param; break;
    case 0x09C0: memcpy(&s_gpu.fog_param[0], &param, 4); break;
    case 0x09C4: memcpy(&s_gpu.fog_param[1], &param, 4); break;
    case 0x17C8:                                  /* CLEAR_REPORT_VALUE */
        s_gpu.zpass_count = 0;
        break;
    case 0x17CC:                                  /* SET_ZPASS_PIXEL_COUNT_ENABLE */
        s_gpu.zpass_enable = param;
        break;
    case 0x17D0: {                                /* GET_REPORT */
        /* The GPU's answer to a visibility test: 16 bytes at the report
         * DMA offset -- a timestamp, the pixel count, and 0 for "done".
         * Never written, a title polling it (D3D's GetVisibilityTestResult
         * starts the slot at 0xFFFFFFFF) sees every test incomplete. Burnout
         * 3 culls its world with these, so the race's main view drew only
         * the sky while the cube-map passes, which do not test, drew it all.
         * The report DMA object starts at physical 0 here, as D3D sets it. */
        uint8_t *mem = (uint8_t *)xbox_GetMemoryOffset();
        uint32_t at = dma_resolve(param & 0x00FFFFFFu);
        uint64_t stamp = (uint64_t)s_gpu.reports++ * 1000u;
        if (getenv("RECOMP_REPORT_TRACE")) {
            static unsigned shown;
            if (shown++ < 32)
                fprintf(stderr, "  [REPORT] param=%08X resolved=%08X count=%u\n",
                        param, at, s_gpu.zpass_count);
        }
        memcpy(mem + at, &stamp, 8);
        memcpy(mem + at + 8, &s_gpu.zpass_count, 4);
        memset(mem + at + 12, 0, 4);
        break;
    }
    case NV097_SET_BLEND_COLOR:
        s_gpu.blend_color = param;
        break;
    case NV097_SET_BLEND_FUNC_SFACTOR:
        s_gpu.blend_sfactor = param;
        break;
    case NV097_SET_BLEND_FUNC_DFACTOR:
        s_gpu.blend_dfactor = param;
        break;
    case NV097_CLEAR_SURFACE:
        if (s_ftrace == 2)
            fprintf(stderr, "[FTRACE] clear %X surf %08X %ux%u colour %08X%c",
                    param, s_gpu.color_offset, s_gpu.clip_w, s_gpu.clip_h,
                    s_gpu.clear_color, 10);
        if (batch_dump_flip(s_gpu.flips))
            fprintf(stderr, "  [BATCH] f%u clear 0x%X on surface 0x%08X to 0x%08X%c", s_gpu.flips,
                    param, dma_resolve(s_gpu.color_offset), s_gpu.clear_color, 10);
        clear_surface(param);
        /* The software rasteriser's depth buffer: 4 MB cleared at every depth clear, about
         * 2 ms a frame on a Steam Deck, for a rasteriser that is off whenever the translator draws. */
        if ((param & 0x01) && soft_raster_on())   /* Z */
            zbuf_clear();
        break;

    case NV097_SET_BEGIN_END:
        if (param) {
            s_gpu.prim = param;
            s_draw_arrays = 0;
            s_gpu.idx_count = 0;
            s_gpu.inline_count = 0;
            s_gpu.imm_count = 0;
        } else {
            /* Three ways a batch can have arrived, and only one is in use at
             * a time: vertices completed by SET_VERTEX4F, a payload written
             * with INLINE_ARRAY, or indices into the title's own arrays. */
            if (s_gpu.imm_count)
                draw_immediate();
            else if (s_gpu.inline_count)
                draw_inline_array();
            else
                draw_primitive();
            s_gpu.prim = 0;
            s_gpu.inline_count = 0;
            s_gpu.imm_count = 0;
        }
        break;

    case NV097_INLINE_ARRAY:
        /* Vertex data, not a pointer to it. Buffered rather than decoded here
         * because the format is only fully known at END. */
        if (s_gpu.prim && s_gpu.inline_count < NV_MAX_INLINE)
            s_gpu.inline_buf[s_gpu.inline_count++] = param;
        break;

    case NV097_ARRAY_ELEMENT16:
        /* Two 16-bit indices per parameter word. */
        if (s_gpu.prim && s_gpu.idx_count + 2 <= NV_MAX_INDICES) {
            s_gpu.idx[s_gpu.idx_count++] = (uint16_t)(param & 0xFFFF);
            s_gpu.idx[s_gpu.idx_count++] = (uint16_t)(param >> 16);
        } else if (s_gpu.prim) {
            /* A batch longer than the index buffer loses its tail. Say so:
             * a silently shortened mesh looks like a modelling choice. */
            static unsigned over;
            if ((++over & (over - 1)) == 0)
                fprintf(stderr, "  [GPU] batch over %d indices truncated (prim %u, #%u)\n",
                        NV_MAX_INDICES, s_gpu.prim, over);
        }
        break;
    case NV097_ARRAY_ELEMENT32:
        /* One index per word. Direct3D sends an odd count's last index this
         * way after the pairs above; dropped, every odd-length strip lost
         * its final triangle. */
        if (s_gpu.prim && s_gpu.idx_count + 1 <= NV_MAX_INDICES)
            s_gpu.idx[s_gpu.idx_count++] = param;
        break;
    case NV097_DRAW_ARRAYS: {
        /* Vertices straight from the arrays, no indices: count - 1 in the top
         * byte, the first vertex below it (Direct3D's DrawPrimitive; a long
         * run comes as several of these inside one BEGIN/END). Unhandled, the
         * whole batch was dropped -- 10,608 of them in Def Jam's opening
         * cutscene. */
        uint32_t first = param & 0x00FFFFFFu, n = (param >> 24) + 1u, k;
        if (!s_gpu.prim)
            break;
        s_draw_arrays = 1;
        for (k = 0; k < n && s_gpu.idx_count < NV_MAX_INDICES; k++)
            s_gpu.idx[s_gpu.idx_count++] = first + k;
        break;
    }

    case NV097_SET_FLIP_READ:
        if (s_ftrace == 2)
            fprintf(stderr, "[FTRACE] P_READ %u%c", param, 10);
        s_gpu.flip_read = param;
        if(nv2a_flip_enabled()) nv2a_flip_set_read(param);
        return;

    case NV097_SET_FLIP_WRITE:
        if (s_ftrace == 2)
            fprintf(stderr, "[FTRACE] P_WRITE %u%c", param, 10);
        s_gpu.flip_write = param;
        if(nv2a_flip_enabled()) nv2a_flip_set_write(param);
        return;

    case NV097_SET_FLIP_MODULO:
        s_gpu.flip_modulo = param;
        if(nv2a_flip_enabled()) nv2a_flip_set_modulo(param);
        return;

    case NV097_FLIP_INCREMENT_WRITE:
        if(point_capture_on())draw_capture_finish(&s_point_capture,0);
        if(hair_capture_on())draw_capture_finish(&s_hair_capture,1);
        s_gpu.flip_write = s_gpu.flip_modulo
                         ? (s_gpu.flip_write + 1) % s_gpu.flip_modulo
                         : s_gpu.flip_write + 1;
        if(nv2a_flip_enabled()) {
            nv2a_flip_increment_write();
            s_gpu.flip_write=(nv2a_flip_surface()>>20)&7u;
        }
        s_gpu.flips++;
        ws_end_frame();
        frame_trace_flip();
        return;

    case NV097_FLIP_STALL:
        if(point_capture_on())draw_capture_finish(&s_point_capture,0);
        if(hair_capture_on())draw_capture_finish(&s_hair_capture,1);
        if (s_ftrace == 2)
            fprintf(stderr, "[FTRACE] FLIP_STALL presenting %08X (color now %08X)%c",
                    s_gpu.drawn_offset, s_gpu.color_offset, 10);
        /* The stall ends when the buffer being read is the one just finished,
         * which on the console is the next vertical blank: a title presenting
         * with an interval of one runs at 60 frames a second because its GPU
         * does. Ended at once, the executor flipped as fast as it could, and
         * the title's frame clock with it -- an optimised build ran a fight's
         * round clock 1.3 times real time. Paced to RECOMP_FLIP_HZ (60; 0 = no
         * wait) on the host's performance counter. */
        if(nv2a_flip_enabled()) {
            /* The guest's vblank handler advances READ_3D via PGRAPH MMIO.
             * A busy scanout buffer cannot be reused until that ack arrives.
             * Publish releases already read before stopping the executor. */
            xbox_Nv2aReleasePump(1);
            nv2a_flip_wait();
            s_gpu.flip_read=(nv2a_flip_surface()>>24)&7u;
        } else {
            flip_pace();
            s_gpu.flip_read = s_gpu.flip_write;
        }
        /* And this is a completed swap, which is what a title's own swap
         * counter counts -- see xbox_Nv2aFrameCounterFlip. */
        xbox_Nv2aFrameCounterFlip();
        /* Hand the window a copy of the frame just finished.
         *
         * The buffer the title has finished is the one the last batch drew
         * into, which is what drawn_offset holds and why it exists: by the
         * flip, color_offset has already moved to the next buffer. Copying
         * here, rather than letting the window read guest memory on its own
         * clock, is what stops it showing a surface the rasteriser is still
         * writing. */
        {
            extern void xbox_FramebufferWindowFrameStats(uint32_t);
            static uint32_t draws_at_flip;
            xbox_FramebufferWindowFrameStats(s_gpu.draws - draws_at_flip);
            draws_at_flip = s_gpu.draws;
        }
        if (s_gpu.pitch) {
            extern void xbox_FramebufferWindowPresent(uint32_t, uint32_t);
            uint32_t done = s_gpu.drawn_offset ? s_gpu.drawn_offset
                                               : s_gpu.color_offset;
            uint32_t pitch = s_gpu.drawn_offset ? s_gpu.drawn_pitch
                                                : s_gpu.pitch;
            if (done) {
                xbox_FramebufferWindowSet(dma_resolve(done), pitch);
                xbox_FramebufferWindowPresent(dma_resolve(done), pitch);
            }
        }
        {
            /* RECOMP_FB_DUMP_FLIPS: dump every presented frame, not one per
             * report -- a consecutive sequence, which is what a headless
             * recording (frames -> ffmpeg) needs. "1" dumps from boot; any
             * other value names a file, and frames are dumped while it
             * exists, so a capture can start mid-game without writing
             * gigabytes of menus first. */
            static const char *spec = (const char *)-1;
            if (spec == (const char *)-1)
                spec = getenv("RECOMP_FB_DUMP_FLIPS");
            if (spec) {
                int on = strcmp(spec, "1") == 0;
                if (!on) {
                    FILE *f = fopen(spec, "rb");
                    if (f) { fclose(f); on = 1; }
                }
                s_flip_dumping = on;
                if (on)
                    dump_surface_bmp();
            }
        }
        s_gpu.drawn_stale = 1;

        /* End of frame is where the translator presents what it batched. */
        {
            static int fwd = -1;
            if (fwd < 0)
                fwd = getenv("RECOMP_PB_D3D11") != NULL;
            if (fwd) {
                /* The host owns the swap chain, so it presents. Keep this
                 * history available through its synchronous post-Present
                 * callback; a black-frame trigger dumps this exact image. */
                const uint32_t completed_color=dma_resolve(
                    s_gpu.drawn_offset?s_gpu.drawn_offset:s_gpu.color_offset);
                /* Preserve the executor's existing completed-surface policy,
                 * independently of the host's last attachment selection.
                 * Hardware scanout/AA identity remains a separate contract. */
#ifdef XBOXRECOMP_VISUAL_EXPORT
                vsj_backend_boundary(VSJ_SCENE_COMPLETED_TARGET,s_gpu.flips,completed_color);
#endif
                pgraph_d3d11_present_surface(completed_color);
                history_record(PB_HISTORY_PRESENT,NV097_FLIP_STALL,completed_color,1);
                pgraph_d3d11_frame_end();
                history_present_complete();
                s_hair_completed_pending=0;
            }
        }
        if (pb_exec_verbose()) {
            static unsigned n;
            if (n++ < 8) {
                fprintf(stderr, "  [GPU] flip %u: read=%u write=%u\n",
                        s_gpu.flips, s_gpu.flip_read, s_gpu.flip_write);
                fflush(stderr);
            }
        }
        return;


    /* Vertex programs and their constants: forwarded as they arrive. */
    case 0x1E94:                                  /* TRANSFORM_EXECUTION_MODE */
        s_gpu.xf_mode = param;
        break;
    case 0x1E98:                                  /* _PROGRAM_CXT_WRITE_EN */
        nv2a_vsh_set_cxt_write(param);
        break;
    case 0x1E9C:                                  /* _PROGRAM_LOAD */
        nv2a_vsh_set_load_slot(param);
        break;
    case 0x1EA0:                                  /* _PROGRAM_START */
        nv2a_vsh_set_start_slot(param);
        break;
    case 0x1EA4:                                  /* _CONSTANT_LOAD */
        nv2a_vsh_set_constant_load(param);
        break;

    /* Depth. */
    case 0x030C: s_gpu.depth_test = param; break; /* DEPTH_TEST_ENABLE */
    case 0x0354: s_gpu.depth_func = param; break; /* DEPTH_FUNC */
    case 0x035C: s_gpu.depth_mask = param; break; /* DEPTH_MASK */
    case 0x0214: s_gpu.zeta_offset = param; break;/* SURFACE_ZETA_OFFSET */
    case 0x1D8C: s_gpu.zstencil_clear = param; break; /* ZSTENCIL_CLEAR_VALUE */
    case 0x1D98: s_gpu.clear_rect_h=param;s_gpu.clear_rect_known|=1;break;
    case 0x1D9C: s_gpu.clear_rect_v=param;s_gpu.clear_rect_known|=2;break;

    default:
        if(method>=0x0AE0 && method<0x0AF0) { /* SET_TEXTURE_COLOR_KEY(stage) */
            unsigned stage=(method-0x0AE0)/4;
            s_gpu.texture_key[stage]=param;
            s_gpu.texture_key_known[stage]=1;
            break;
        }
        if (method >= 0x0260 && method < 0x0280) {         /* ALPHA_ICW(i) */
            s_gpu.rc.alpha_icw[(method - 0x0260) / 4] = param;
            break;
        }
        if (method >= 0x0AC0 && method < 0x0AE0) {         /* COLOR_ICW(i) */
            s_gpu.rc.color_icw[(method - 0x0AC0) / 4] = param;
            break;
        }
        if (method >= 0x1E40 && method < 0x1E60) {         /* COLOR_OCW(i) */
            s_gpu.rc.color_ocw[(method - 0x1E40) / 4] = param;
            break;
        }
        if (method >= 0x0AA0 && method < 0x0AC0) {         /* ALPHA_OCW(i) */
            s_gpu.rc.alpha_ocw[(method - 0x0AA0) / 4] = param;
            break;
        }
        if (method >= 0x0A60 && method < 0x0A80) {         /* FACTOR0(i) */
            s_gpu.rc.factor0[(method - 0x0A60) / 4] = param;
            break;
        }
        if (method >= 0x0A80 && method < 0x0AA0) {         /* FACTOR1(i) */
            s_gpu.rc.factor1[(method - 0x0A80) / 4] = param;
            break;
        }
        /* SET_VIEWPORT_OFFSET / _SCALE. The GPU keeps these in the constant
         * file, at c[59] and c[58] -- exactly where the D3D epilogue reads
         * them -- so they are constants that happen to arrive by method.
         * Dropped, every vertex program put its whole batch at the origin:
         * no 3D anywhere, and a black car on the garage screen. */
        if (method >= 0x0A20 && method < 0x0A30) {
            nv2a_vsh_constant_component(59, (method - 0x0A20) / 4, param);
            break;
        }
        if (method >= 0x0AF0 && method < 0x0B00) {
            nv2a_vsh_constant_component(58, (method - 0x0AF0) / 4, param);
            break;
        }
        if (method >= 0x0B00 && method < 0x0B80) {    /* TRANSFORM_PROGRAM(i) */
            nv2a_vsh_program_word(param);
            break;
        }
        if (method >= 0x0B80 && method < 0x0C00) {    /* TRANSFORM_CONSTANT(i) */
            nv2a_vsh_constant_word(param);
            break;
        }
        if (method >= NV_TEX_FIRST && method <= NV_TEX_LAST)
            record_tex_reg(method, param);
        if (method >= NV097_SET_VERTEX_DATA_ARRAY_OFFSET
                && method < NV097_SET_VERTEX_DATA_ARRAY_OFFSET + NV_VERTEX_ATTRS * 4) {
            /* Resolved here, once, so every consumer -- the rasteriser's
             * attribute reads and the diagnostics alike -- sees the same
             * address. A vertex array offset is a DMA-object offset exactly
             * like a surface offset: physical, and addressable only through
             * the window when it names contiguous memory. */
            s_gpu.attr[(method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4].offset =
                dma_resolve(param);
        } else if (method >= NV097_SET_VERTEX_DATA_ARRAY_FORMAT
                && method < NV097_SET_VERTEX_DATA_ARRAY_FORMAT + NV_VERTEX_ATTRS * 4) {
            VertexAttr *a = &s_gpu.attr[(method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4];
            a->type   =  param        & 0x0F;
            a->size   = (param >> 4)  & 0x0F;
            a->stride = (param >> 8)  & 0xFF;
        } else if (!imm_vertex_method(method, param)) {
            note_unhandled(method, param);
        }
        break;
    }
    if(method>=NV097_SET_SURFACE_CLIP_HORIZONTAL && method<=0x0214u /* zeta offset */ && !(method&3))
        history_record(PB_HISTORY_TARGET,method,param,1);
}

/* Find where the title actually wrote its quad.
 *
 * The GPU is pointed at a buffer that stays zero, which says the data went
 * somewhere else -- and the only way to find somewhere else is to look for the
 * data. A screen-space quad for a 640x480 target contains 640.0f and 480.0f as
 * floats, which is a distinctive enough pair to search guest RAM for. Whatever
 * address that turns up is where the title's writes are landing, and the
 * difference from the programmed offset is the bug. */
static void find_quad_vertices(void)
{
    const uint32_t W = 0x44200000u;   /* 640.0f */
    const uint32_t H = 0x43F00000u;   /* 480.0f */
    const uint32_t *ram = (const uint32_t *)xbox_GetMemoryOffset();
    uint32_t i, hits = 0;

    fprintf(stderr, "[GPU] searching guest RAM for 640.0f/480.0f pairs...\n");
    for (i = 0x1000 / 4; i < (0x04000000u / 4) - 8 && hits < 12; i++) {
        if (ram[i] != W && ram[i] != H)
            continue;
        /* Both values within a few words of each other: a lone 640.0f is
         * common, the pair much less so. */
        {
            int has_w = 0, has_h = 0;
            uint32_t k;
            for (k = 0; k < 8; k++) {
                if (ram[i + k] == W) has_w = 1;
                if (ram[i + k] == H) has_h = 1;
            }
            if (!has_w || !has_h)
                continue;
        }
        hits++;
        fprintf(stderr, "  [GPU]   0x%08X:", i * 4);
        {
            uint32_t k;
            for (k = 0; k < 8; k++)
                fprintf(stderr, " %08X", ram[i + k]);
        }
        fprintf(stderr, "\n");
        i += 8;
    }
    if (!hits)
        fprintf(stderr, "  [GPU]   none found -- the quad is not in RAM in"
                        " that form\n");
    fflush(stderr);
}

/* Locate NaN-filled transform matrices in guest RAM.
 *
 * A matrix arriving as NaN says the maths went wrong somewhere upstream, and
 * the only way to find where is to find the matrix and watch who writes it.
 * Three consecutive real-indefinite values is a distinctive enough signature:
 * ordinary data does not contain runs of 0xFFC00000. */
static void find_nan_matrices(void)
{
    const uint32_t NAN_NEG = 0xFFC00000u;
    const uint32_t *ram = (const uint32_t *)xbox_GetMemoryOffset();
    uint32_t i, hits = 0;

    fprintf(stderr, "[GPU] searching guest RAM for NaN matrices...\n");
    for (i = 0x1000 / 4; i < (0x04000000u / 4) - 20 && hits < 10; i++) {
        if (ram[i] != NAN_NEG || ram[i + 1] != NAN_NEG || ram[i + 2] != NAN_NEG)
            continue;
        hits++;
        fprintf(stderr, "  [GPU]   0x%08X:", i * 4);
        {
            uint32_t k;
            for (k = 0; k < 16; k++)
                fprintf(stderr, " %08X", ram[i + k]);
        }
        fprintf(stderr, "\n");
        i += 16;
    }
    if (!hits)
        fprintf(stderr, "  [GPU]   none in RAM -- the NaNs are computed into"
                        " registers, not stored\n");
    fflush(stderr);
}

/* Print guest dwords named by RECOMP_PEEK, as hex and as float.
 *
 * Chasing a value backwards means reading it, and a value that is only wrong
 * for one frame in a thousand cannot be caught by stopping. Both
 * interpretations are printed because the question is usually "is this a
 * pointer or a number", and guessing wrong costs a run. */
static void peek_addresses(void)
{
    const char *spec = getenv("RECOMP_PEEK");
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    char buf[256];
    char *tok, *ctx = NULL;

    if (!spec)
        return;
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_s(buf, ",", &ctx); tok; tok = strtok_s(NULL, ",", &ctx)) {
        uint32_t va = (uint32_t)strtoul(tok, NULL, 0);
        uint32_t v;
        float f;
        /* The low window, or the contiguous window where physical RAM
         * is mapped. Objects a title allocates for DMA live in the
         * second one, and skipping it silently made this print nothing
         * at all for an address that was perfectly readable. */
        if (!((va >= 0x1000u && va < 0x04000000u)
           || (va >= 0x80000000u && va < 0x84000000u)))
            continue;
        v = *(const uint32_t *)(mem + va);
        memcpy(&f, &v, 4);
        fprintf(stderr, "  [PEEK] 0x%08X = %08X  (%g)\n", va, v, f);
    }
    fflush(stderr);
}

/* Walk a pointer chain and print every step.
 *
 * RECOMP_PEEK_CHAIN="0x1315A8,8,0x10,0" starts at that address, and for each
 * offset dereferences the current pointer and adds it. Following a chain by
 * hand costs one run per level; this costs one run for the whole chain, and
 * prints where it goes wrong when a level is null.
 */
static void peek_chain(void)
{
    const char *spec = getenv("RECOMP_PEEK_CHAIN");
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    char buf[256], *tok, *ctx = NULL;
    uint32_t cur = 0;
    int step = 0;

    if (!spec)
        return;
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    for (tok = strtok_s(buf, ",", &ctx); tok; tok = strtok_s(NULL, ",", &ctx)) {
        uint32_t off = (uint32_t)strtoul(tok, NULL, 0);
        if (step == 0) {
            cur = off;
            fprintf(stderr, "  [CHAIN] start 0x%08X\n", cur);
        } else {
            /* Both windows, as peek_addresses reads them: objects a title
             * allocates contiguously live at 0x8xxxxxxx, and a chain that
             * stops at the first of those stops at most of them. */
            if (!((cur >= 0x1000u && cur + 4 < 0x04000000u)
               || (cur >= 0x80000000u && cur + 4 < 0x84000000u))) {
                fprintf(stderr, "  [CHAIN] step %d: 0x%08X is not a guest"
                                " pointer -- chain ends\n", step, cur);
                return;
            }
            cur = *(const uint32_t *)(mem + cur) + off;
            fprintf(stderr, "  [CHAIN] step %d: deref +0x%X -> 0x%08X\n",
                    step, off, cur);
        }
        step++;
    }
    if ((cur >= 0x1000u && cur + 4 < 0x04000000u)
     || (cur >= 0x80000000u && cur + 4 < 0x84000000u))
        fprintf(stderr, "  [CHAIN] final value at 0x%08X = 0x%08X\n",
                cur, *(const uint32_t *)(mem + cur));
    fflush(stderr);
}

void nv2a_pb_exec_report(void)
{
    peek_addresses();
    peek_chain();
    if (getenv("RECOMP_FIND_NAN")) {
        /* Every report, not once: the matrix is fine early on and only turns
         * to NaN later, so a single scan at startup finds nothing and says
         * nothing. */
        find_nan_matrices();
    }
    if (getenv("RECOMP_FIND_QUAD")) {
        static int done;
        if (!done) { done = 1; find_quad_vertices(); }
    }
    int i, j;

    {
        /* Frames per second of guest time, from flips between reports. */
        static uint32_t last_flips;
        static unsigned long last_ms;
        unsigned long now = (unsigned long)(clock() * 1000.0 / CLOCKS_PER_SEC);
        if (last_ms && now > last_ms)
            fprintf(stderr, "[GPU] %.2f fps (%u flips)%c",
                    (s_gpu.flips - last_flips) * 1000.0 / (now - last_ms),
                    s_gpu.flips, 10);
        last_flips = s_gpu.flips;
        last_ms = now;
    }
    fprintf(stderr, "[GPU] flip %u surface 0x%08X format 0x%X pitch %u clip %ux%u+%u+%u"
                    " clears %u | %u unhandled methods (%d distinct)\n",
            s_gpu.flips, s_gpu.color_offset, s_gpu.format, s_gpu.pitch, s_gpu.clip_w, s_gpu.clip_h,
            s_gpu.clip_x, s_gpu.clip_y, s_gpu.clears,
            s_gpu.unhandled_total, s_unhandled_count);
    fprintf(stderr, "[GPU] draws %u (%u with coordinates), %u indices;"
                    " x %.1f..%.1f  y %.1f..%.1f\n",
            s_gpu.draws, s_gpu.nonzero_draws, s_gpu.verts,
            s_gpu.min_x, s_gpu.max_x, s_gpu.min_y, s_gpu.max_y);
    /* One picture per report rather than per clear: a title clears hundreds of
     * times a second and nobody wants that many files. */
    if (!s_flip_dumping)
        dump_surface_bmp();

    /* Drawn and skipped separately: "nothing appeared" and "every batch needed
     * a vertex program we do not run" look identical on screen, and only one
     * of them means the rasteriser is broken. */
    fprintf(stderr, "[GPU] brightest pixel written 0x%08X\n", s_gpu.pixel_max);
    fprintf(stderr, "[GPU] %llu pixels written; draw surface 0x%08X"
                    " -> 0x%08X, clear surface 0x%08X -> 0x%08X\n",
            (unsigned long long)s_gpu.pixels, s_gpu.drawn_offset,
            dma_resolve(s_gpu.drawn_offset), s_gpu.color_offset,
            dma_resolve(s_gpu.color_offset));
    fprintf(stderr, "[GPU] rasterised %u triangles; %u batches skipped as not"
                    " screen-space, %u triangles fully off-surface\n",
            s_gpu.tris_drawn, s_gpu.batches_untransformed,
            s_gpu.tris_skipped_offscreen);
    fprintf(stderr, "[GPU] vertex programs: %u batches, %u vertices;"
                    " %u triangles dropped behind the eye\n",
            s_gpu.batches_program, s_gpu.verts_program, s_gpu.tris_behind);
    fprintf(stderr, "[GPU]   of the rest: %u degenerate/NaN, %u off-surface,"
                    " %u drawn; pixels %llu written, %llu depth-failed;"
                    " x %.0f..%.0f y %.0f..%.0f z %g..%g\n",
            s_gpu.xf_degenerate, s_gpu.xf_offscreen, s_gpu.xf_drawn,
            (unsigned long long)s_gpu.xf_pixels,
            (unsigned long long)s_gpu.xf_depth_fail,
            s_gpu.xf_min[0], s_gpu.xf_max[0], s_gpu.xf_min[1], s_gpu.xf_max[1],
            s_gpu.xf_min[2], s_gpu.xf_max[2]);
    {
        fprintf(stderr, "[GPU]   blend pairs (src/dst):");
        for (j = 0; j < s_gpu.blend_npairs; j++)
            fprintf(stderr, " %X/%X", s_gpu.blend_pairs[j] >> 16,
                    s_gpu.blend_pairs[j] & 0xFFFF);
        fputc(10, stderr);
        fprintf(stderr, "[GPU]   program attribute formats (slot:type/size):");
        for (j = 0; j < s_gpu.xf_nfmt; j++)
            fprintf(stderr, " v%u:%u/%u", s_gpu.xf_fmt[j] >> 8,
                    s_gpu.xf_fmt[j] & 15, (s_gpu.xf_fmt[j] >> 4) & 15);
        fprintf(stderr, "\n");
    }

    /* And of the batches that did rasterise, how many sampled anything. A menu
     * that draws its background from one texture and its text from another
     * shows both as flat colour if either half is missing, so the split is
     * what says which half. */
    fprintf(stderr, "[GPU] batches: %u textured, %u with no texcoords,"
                    " %u with texcoords but no usable stage\n",
            s_gpu.batches_textured, s_gpu.batches_no_uv, s_gpu.batches_no_tex);
    for (i = 0; i < s_tex_use_count; i++)
        fprintf(stderr, "  [TEXUSE] 0x%08X %ux%u fmt 0x%02X%s: %u batches\n",
                s_tex_use[i].offset, s_tex_use[i].width, s_tex_use[i].height,
                s_tex_use[i].color,
                d3d8_format_dxt_block_bytes(s_tex_use[i].color) ? " dxt"
                    : d3d8_format_is_swizzled(s_tex_use[i].color) ? " swz" : " lin",
                s_tex_use[i].batches);

    if (getenv("RECOMP_TEX_STATE")) {
        uint32_t k;
        for (k = 0; k < sizeof s_tex_set / sizeof s_tex_set[0]; k++)
            if (s_tex_set[k])
                fprintf(stderr, "  [TEX] 0x%04X = 0x%08X\n",
                        (unsigned)(NV_TEX_FIRST + k * 4), s_tex_reg[k]);
    }

    /* Top ten by frequency: selection sort over a small table, once every few
     * seconds, is not worth a better algorithm.
     *
     * RECOMP_PB_UNHANDLED_ALL lists every one instead. Ten is the right
     * default -- the tail is a long list of state registers nobody needs to
     * read -- but when a title stops and the question is which method it
     * stopped on, the answer is as likely to be the one seen twice as the
     * one seen a thousand times, and ten hides it. */
    {
        int shown = getenv("RECOMP_PB_UNHANDLED_ALL") ? s_unhandled_count : 10;
    for (i = 0; i < shown && i < s_unhandled_count; i++) {
        int best = i;
        for (j = i + 1; j < s_unhandled_count; j++)
            if (s_unhandled[j].count > s_unhandled[best].count)
                best = j;
        if (best != i) {
            PbUnhandled t = s_unhandled[i];
            s_unhandled[i] = s_unhandled[best];
            s_unhandled[best] = t;
        }
        {
            /* The value as well as the count. A method nobody decoded is
             * a guess until you see what it carried: screen coordinates,
             * a 0..1 texcoord and a packed colour are told apart at a
             * glance, and that is what says which vertex encoding a title
             * is using. */
            union { uint32_t u; float f; } v;
            v.u = s_unhandled[i].last_param;
            fprintf(stderr, "  [GPU]   0x%04X x%-8u last=0x%08X (%.4f)\n",
                    s_unhandled[i].method, s_unhandled[i].count,
                    v.u, v.f);
        }
    }
    }    fflush(stderr);
}
