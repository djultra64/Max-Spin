/*
 * Read-only survey of the pushbuffer a title submits.
 *
 * The title builds NV2A commands in guest RAM and advances DMA_PUT; nothing
 * here executes them, so the framebuffer stays black however far the game
 * gets. Before any of that can be made to draw, the question is what it
 * actually asks for -- which methods, on which object classes, how many of
 * them -- because that is the difference between "the existing PGRAPH
 * translator nearly covers this" and "this needs a real one".
 *
 * Purely a reader: it walks the buffer and counts, and never writes to guest
 * memory or to the GPU state. Enabled with RECOMP_PB_SCAN.
 *
 * Pushbuffer encoding (NV20/NV2A), one dword per command header:
 *   (w & 0xE0030003) == 0x00000000  increasing methods
 *   (w & 0xE0030003) == 0x40000000  non-increasing (same method, count params)
 *   (w & 0x00000003) == 0x00000001  jump
 *   (w & 0x00000003) == 0x00000002  call
 *   (w & 0xFFFF0003) == 0x00020000  return
 * For a method header: count = (w >> 18) & 0x7FF, subchannel = (w >> 13) & 7,
 * method = w & 0x1FFC.
 */
#include "visual_pb_hooks.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>
#include <string.h>

#ifndef XBOX_CONTIG_BASE
#define XBOX_CONTIG_BASE 0x80000000u   /* the contiguous window; kernel.h */
#endif
#define XBOX_CONTIG_WINDOW 0x04000000u  /* 64 MB of it */

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define PB_MAX_METHODS 4096

static struct { uint32_t method, subch, count; } s_seen[PB_MAX_METHODS];
static int s_seen_count;

/* Parse health. An inventory is only worth reading if the walk stayed in step
 * with the command stream: a decoder that desynchronises produces plausible
 * looking method numbers out of parameter data, and the counts then describe
 * nothing. Unrecognised words are the tell. */
static uint32_t s_tot_words, s_tot_unknown, s_tot_jumps, s_tot_segments;

/* Executing is opt-in separately from surveying: a survey is read-only, while
 * the executor writes to guest memory. */
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_report(void);
static int s_exec_enabled = -1;

static void note(uint32_t subch, uint32_t method)
{
    /* Called for every parameter word in the stream, so the entry is found by
     * index, not by walking the table: with a few hundred distinct methods
     * the walk was two fifths of the executor thread's time in a busy scene.
     * Methods are 11 bits of dword offset on one of eight subchannels. */
    static uint16_t slot[8][0x800];
    uint16_t *at = &slot[subch & 7u][(method >> 2) & 0x7FFu];
    if (*at) {
        s_seen[*at - 1].count++;
        return;
    }
    if (s_seen_count < PB_MAX_METHODS)
        *at = (uint16_t)(s_seen_count + 1);
    if (s_seen_count >= PB_MAX_METHODS) {
        /* Silently dropping past the cap is how a truncated inventory reads as
         * "the title never does that" -- exactly the wrong conclusion when the
         * inventory is being used to decide what to implement. */
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[PB] method table full at %d -- inventory is"
                            " truncated\n", PB_MAX_METHODS);
        }
    }
    if (s_seen_count < PB_MAX_METHODS) {
        s_seen[s_seen_count].method = method;
        s_seen[s_seen_count].subch  = subch;
        s_seen[s_seen_count].count  = 1;
        s_seen_count++;
    }
}

/* NV097 (Kelvin 3D class) methods worth naming. The point of the survey is to
 * decide what a translator has to implement, and a bare method number does not
 * answer that -- "0x1808 x412" only means something once it reads
 * INLINE_ARRAY. Unnamed ones still get counted. */
static const struct { uint32_t m; const char *name; } NV097_NAMES[] = {
    { 0x0000, "SET_OBJECT" },
    { 0x0100, "NO_OPERATION" },
    { 0x0104, "SET_WARNING_ENABLE" },
    { 0x0130, "SET_FLIP_READ" },
    { 0x0200, "SET_SURFACE_CLIP_HORIZONTAL" },
    { 0x0204, "SET_SURFACE_CLIP_VERTICAL" },
    { 0x0208, "SET_SURFACE_FORMAT" },
    { 0x020C, "SET_SURFACE_PITCH" },
    { 0x0210, "SET_SURFACE_COLOR_OFFSET" },
    { 0x0214, "SET_SURFACE_ZETA_OFFSET" },
    { 0x0300, "SET_ALPHA_TEST_ENABLE" },
    { 0x0304, "SET_BLEND_ENABLE" },
    { 0x030C, "SET_DEPTH_TEST_ENABLE" },
    { 0x0310, "SET_DITHER_ENABLE" },
    { 0x0314, "SET_LIGHTING_ENABLE" },
    { 0x033C, "SET_CULL_FACE_ENABLE" },
    { 0x0340, "SET_DEPTH_MASK" },
    { 0x0350, "SET_CLEAR_DEPTH_VALUE" },
    { 0x1D8C, "SET_CLEAR_DEPTH" },
    { 0x1D90, "SET_COLOR_CLEAR_VALUE" },
    { 0x1D94, "CLEAR_SURFACE" },
    { 0x1D6C, "SET_ZSTENCIL_CLEAR" },
    { 0x0B80, "SET_TRANSFORM_PROGRAM" },
    { 0x0B00, "SET_TRANSFORM_CONSTANT" },
    { 0x1720, "SET_VERTEX_DATA_ARRAY_OFFSET" },
    { 0x1760, "SET_VERTEX_DATA_ARRAY_FORMAT" },
    { 0x17FC, "SET_BEGIN_END" },
    { 0x1800, "ARRAY_ELEMENT16" },
    { 0x1808, "INLINE_ARRAY" },
    { 0x1810, "DRAW_ARRAYS" },
    { 0x1B00, "SET_TEXTURE_OFFSET" },
    { 0x1B04, "SET_TEXTURE_FORMAT" },
    { 0x1B08, "SET_TEXTURE_ADDRESS" },
    { 0x1B0C, "SET_TEXTURE_CONTROL0" },
    { 0x1B14, "SET_TEXTURE_IMAGE_RECT" },
    { 0x0FD8, "SET_COMBINER_*" },
    { 0x0000, NULL },
};

static const char *nv097_name(uint32_t m)
{
    int i;
    for (i = 0; NV097_NAMES[i].name; i++)
        if (NV097_NAMES[i].m == m)
            return NV097_NAMES[i].name;
    return "";
}

void nv2a_pb_scan_report(void)
{
    int i;

    if (s_exec_enabled > 0)
        nv2a_pb_exec_report();
    if (!s_seen_count || !getenv("RECOMP_PB_SCAN"))
        return;
    fprintf(stderr, "[PB] %u segments, %u words, %u jumps, %u unrecognised"
                    " -- %d distinct (subchannel, method) pairs\n",
            s_tot_segments, s_tot_words, s_tot_jumps, s_tot_unknown,
            s_seen_count);
    for (i = 0; i < s_seen_count; i++)
        fprintf(stderr, "  [PB]   subch %u  method 0x%04X  x%-6u %s\n",
                s_seen[i].subch, s_seen[i].method, s_seen[i].count,
                nv097_name(s_seen[i].method));
    fflush(stderr);
}

/* The GPU stops at a software method until the title has handled it.
 *
 * Direct3D relies on that. Its fixup method (Def Jam: case 13 of
 * sub_002234F0) patches commands *after* the NOP before the GPU reaches them,
 * from a record the title may reuse the moment the GPU is past; run past the
 * NOP and the patch lands late, on commands already executed, from a record
 * already recycled -- which is how D3D's own DPC came to write to 0x858B003B
 * a few seconds into the main menu. So the executor stops at the end of the
 * packet that queued one, and the caller resumes from there once the title
 * has acknowledged it. RECOMP_PB_NO_STALL=1 restores running straight on. */
static volatile long s_stall_request;

/* The pusher's one-deep subroutine: CALL saves the address after it and
 * jumps, RETURN goes back. Direct3D runs its precompiled push buffers this
 * way (RunPushBuffer), and those are what its fixups patch. Both used to be
 * skipped as if they were no-ops, so every subroutine went unexecuted --
 * whole screens of 3D, among them Def Jam's main menu. */
static uint32_t s_sub_return;
/* The last few transfers of control, for the report when the walk leaves the
 * command stream. */
static struct { uint32_t va, word; } s_hdrs[24];
static unsigned s_hdrs_n;
static struct { uint32_t from, word, to; struct { uint32_t va, word; } hdr[24]; } s_xfer[8];
static unsigned s_xfer_n;
static struct { uint32_t from, put, stop; } s_runs[8];
static unsigned s_runs_n;
/* The recent transfers and runs, for a report from the caller. */
/* The address of the newest transfer taken from the ring (not from inside a
 * subroutine): where a bad CALL or JUMP was read, for RECOMP_PB_DUMP_XFER. */
uint32_t nv2a_pb_last_ring_xfer(uint32_t lo, uint32_t hi)
{
    unsigned k;
    for (k = 0; k < 8 && k < s_xfer_n; k++) {
        unsigned i = (s_xfer_n - 1 - k) & 7u;
        if (s_xfer[i].from >= lo && s_xfer[i].from < hi)
            return s_xfer[i].from;
    }
    return 0;
}

void nv2a_pb_dump_history(void)
{
    unsigned k;
    for (k = 0; k < 8 && k < s_xfer_n; k++) {
        unsigned i = (s_xfer_n - 1 - k) & 7u;
        fprintf(stderr, "  [PB]   transfer -%u: 0x%08X at 0x%08X -> 0x%08X%c", k + 1,
                s_xfer[i].word, s_xfer[i].from, s_xfer[i].to, 10);
        {
            unsigned h;
            for (h = 0; h < 24; h++)
                if (s_xfer[i].hdr[h].va)
                    fprintf(stderr, "  [PB]      after header 0x%08X at 0x%08X (now 0x%08X)%c",
                            s_xfer[i].hdr[h].word, s_xfer[i].hdr[h].va,
                            *(const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + s_xfer[i].hdr[h].va), 10);
        }
    }
    for (k = 0; k < 0 && k < s_hdrs_n; k++) {
        unsigned i = (s_hdrs_n - 1 - k) % 6u;
        fprintf(stderr, "  [PB]   header -%u: 0x%08X at 0x%08X (now 0x%08X)%c", k + 1, s_hdrs[i].word,
                s_hdrs[i].va, *(const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + s_hdrs[i].va), 10);
    }
    for (k = 0; k < 8 && k < s_runs_n; k++) {
        unsigned i = (s_runs_n - 1 - k) & 7u;
        fprintf(stderr, "  [PB]   run -%u: from 0x%08X toward PUT 0x%08X, stopped 0x%08X%c", k + 1,
                s_runs[i].from, s_runs[i].put, s_runs[i].stop, 10);
    }
}
static void note_xfer(uint32_t from, uint32_t word, uint32_t to)
{
    unsigned i = s_xfer_n++ & 7u;
    s_xfer[i].from = from; s_xfer[i].word = word; s_xfer[i].to = to;
    {
        unsigned k;
        for (k = 0; k < 24; k++) {                 /* the headers before it, newest first */
            unsigned j = (s_hdrs_n - 1 - k) % 24u;
            s_xfer[i].hdr[k].va = s_hdrs_n > k ? s_hdrs[j].va : 0;
            s_xfer[i].hdr[k].word = s_hdrs_n > k ? s_hdrs[j].word : 0;
        }
    }
}
static int s_no_call = -1;      /* RECOMP_PB_NO_CALL=1: skip CALL/RETURN, as before */
static int s_sub_active;
static int s_stalled;

/* Where the main stream stands: inside a subroutine that is the word after the
 * CALL, not the subroutine's own address. Returns 1 and sets *ret when inside
 * one. Positions in Direct3D's ring are only comparable with this. */
int nv2a_pb_in_subroutine(uint32_t *ret)
{
    if (!s_sub_active)
        return 0;
    *ret = s_sub_return;
    return 1;
}

/* The caller moved the executor somewhere else (a skip to PUT, a rewind):
 * whatever subroutine it was in is no longer being run. Left set, every CALL
 * after it was refused as nested and every subroutine went unexecuted. */
void nv2a_pb_leave_subroutine(void)
{
    s_sub_active = 0;
}

void nv2a_pb_stall(void)
{
    static int off = -1;
    if (off < 0)
        off = getenv("RECOMP_PB_NO_STALL") != NULL;
    if (!off)
        s_stall_request = 1;
}

/* A word of the stream (a guest address in the contiguous window). */
uint32_t nv2a_pb_read(uint32_t va)
{
    return *(const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + va);
}

/* Decode one run of the push buffer, [va, end_va), handing every method to
 * the executor. Stops at end_va or at a jump; returns where it stopped, and
 * for a jump puts the target's guest address in *jump_va (0 otherwise). */
/* A packet cut off by PUT. The pusher keeps its method, subchannel and
 * remaining count when it stops at PUT and goes on with parameters when PUT
 * moves; Direct3D kicks in the middle of long packets (a large inline vertex
 * array filled across several reservations). The walker started every run
 * afresh, took the next parameter for a header, and lost the stream -- one of
 * the ways into "not a command" and "skipping to PUT". Kept here and resumed
 * only at the exact address it stopped at; any other start discards it. */
static struct {
    uint32_t va, count, method, subch;
    int noninc;
} s_pend;

/* Where the walk is, word by word, for whoever asks how far behind it is
 * (xbox_Nv2aBacklog): a run can take many milliseconds, and its start alone
 * made a moving executor look stopped. */
volatile uint32_t g_pb_live_va;
/* Words the walk has consumed, ever: how far past a fence's release it has
 * read, for the prefetch model (xbox_Nv2aReleasePump). */
volatile uint32_t g_pb_words;

/* One original parser body, two compile-time observation policies. */
#define PB_SEGMENT_NAME pb_segment_plain
#define PB_READ_HEADER(at) nv2a_pb_read(at)
#define PB_READ_ARGUMENT(at) nv2a_pb_read(at)
#include "nv2a_pb_segment.inc"
#undef PB_SEGMENT_NAME
#undef PB_READ_HEADER
#undef PB_READ_ARGUMENT
#define PB_SEGMENT_NAME pb_segment_observed
#define PB_READ_HEADER(at) vpli_read_header_impl(at,nv2a_pb_read)
#define PB_READ_ARGUMENT(at) vpli_read_argument_impl(at,nv2a_pb_read)
#include "nv2a_pb_segment.inc"
#undef PB_SEGMENT_NAME
#undef PB_READ_HEADER
#undef PB_READ_ARGUMENT
static uint32_t pb_segment(uint32_t va,uint32_t end_va,uint32_t *jump_va){
 return vpli_enabled_bridge?pb_segment_observed(va,end_va,jump_va):pb_segment_plain(va,end_va,jump_va);
}

static int pb_enabled(void)
{
    static int scan = -1;              /* once: getenv is slow, and this is per run */
    if (s_exec_enabled < 0)
        s_exec_enabled = getenv("RECOMP_PB_EXEC") != NULL;
    if (scan < 0)
        scan = getenv("RECOMP_PB_SCAN") != NULL;
    return scan || s_exec_enabled;
}

void nv2a_pb_scan(uint32_t start_va, uint32_t end_va)
{
    uint32_t jump;

    if (!pb_enabled() || end_va <= start_va)
        return;
    if (end_va - start_va > 0x400000u)        /* a sane single-frame bound */
        end_va = start_va + 0x400000u;
    pb_segment(start_va, end_va, &jump);
}

/* Run the push buffer from where the GPU last stopped to PUT, following jumps,
 * as the pusher does.
 *
 * The scan above stops at a jump, and its caller only ran it while PUT moved
 * forward. Direct3D wraps its push buffer with a jump back to the start once
 * it fills, so everything it wrote after a wrap was never executed -- and the
 * first thing it writes there is the NOP(5) whose software method releases its
 * wait for space. Def Jam stopped for good at about frame 99 in some runs,
 * waiting on that event, once its first screen filled the buffer.
 *
 * Returns where the run stopped and sets *stalled when that was a software
 * method (see nv2a_pb_stall); otherwise it ran to PUT, or as far as it could. */
uint32_t nv2a_pb_run(uint32_t get_va, uint32_t put_va, int *stalled)
{
    uint32_t va = get_va, jump;
    int hops = 0;

    *stalled = 0;
    if (!pb_enabled() || get_va == put_va)
        return put_va;
    {
        unsigned i = s_runs_n++ & 7u;
        s_runs[i].from = get_va; s_runs[i].put = put_va; s_runs[i].stop = 0;
    }
    while (va != put_va && hops < 4096) {           /* a CALL and a RETURN are two */
        uint32_t end = put_va;
        /* A run up to PUT when PUT is ahead; otherwise PUT is behind a jump,
         * so run until that jump (bounded, as the scan always was). */
        if (put_va < va)
            end = va + 0x400000u;
        s_stalled = 0;
        va = pb_segment(va, end, &jump);
        if (s_stalled) {
            *stalled = 1;
            s_runs[(s_runs_n - 1) & 7u].stop = va;
            return va;
        }
        if (!jump)
            break;
        va = jump;
        hops++;
    }
    return va;
}
