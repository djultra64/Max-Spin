static uint32_t PB_SEGMENT_NAME(uint32_t va, uint32_t end_va, uint32_t *jump_va)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t words = 0, jumps = 0, unknown = 0;
    (void)mem;                                  /* reads go through nv2a_pb_read */

    *jump_va = 0;
    if (s_no_call < 0)
        s_no_call = getenv("RECOMP_PB_NO_CALL") != NULL;
    if (s_pend.count) {
        if (va != s_pend.va) {
            s_pend.count = 0;                   /* the walk moved elsewhere */
        } else {
            while (s_pend.count && va != end_va) {
                note(s_pend.subch, s_pend.method);
                if (s_exec_enabled)
                    nv2a_pb_exec_method(s_pend.subch, s_pend.method, PB_READ_ARGUMENT(va));
                if (!s_pend.noninc)
                    s_pend.method += 4;
                s_pend.count--;
                va += 4;
                words++;
            }
            if (s_pend.count) {
                s_pend.va = va;                 /* still cut off */
                s_tot_words += words;
                return va;
            }
            if (s_stall_request) {
                s_stall_request = 0;
                s_stalled = 1;
                s_tot_words += words;
                return va;
            }
        }
    }
    /* Never outside the contiguous window, where every push buffer lives: a
     * stray jump or a PUT that moved backwards must not walk off it. */
    while (va != end_va && words < 0x100000u
           && va >= XBOX_CONTIG_BASE && va < XBOX_CONTIG_BASE + XBOX_CONTIG_WINDOW) {
        uint32_t w = PB_READ_HEADER(va);
        g_pb_live_va = va;
        va += 4;
        words++;
        if ((++g_pb_words & 255u) == 0) {
            extern void xbox_Nv2aReleasePump(int at_put);
            xbox_Nv2aReleasePump(0);
        }

        if ((w & 3u) == 1u || (w & 0xE0000003u) == 0x20000000u) {
            /* JUMP, new form (target in 31:2) or old (28:2). The target is a
             * physical offset, and the push buffer is contiguous memory. */
            uint32_t target = ((w & 3u) == 1u) ? (w & 0xFFFFFFFCu) : (w & 0x1FFFFFFCu);
            if ((target & 0x0FFFFFFFu) < XBOX_CONTIG_WINDOW)
                *jump_va = XBOX_CONTIG_BASE | (target & 0x0FFFFFFFu);
            note_xfer(va - 4, w, *jump_va);
            jumps++;
            break;
        }
        if ((w & 3u) == 2u && s_no_call) {             /* CALL, ignored on request */
            continue;
        }
        if ((w & 3u) == 2u) {                          /* CALL */
            uint32_t target = w & 0xFFFFFFFCu;
            if (s_sub_active || (target & 0x0FFFFFFFu) >= XBOX_CONTIG_WINDOW) {
                static int told;
                if (told++ < 3)
                    fprintf(stderr, "  [PB] CALL 0x%08X at 0x%08X %s; skipped\n", w, va - 4,
                            s_sub_active ? "inside a subroutine" : "outside memory");
                continue;
            }
            s_sub_return = va;
            s_sub_active = 1;
            *jump_va = XBOX_CONTIG_BASE | (target & 0x0FFFFFFFu);
            note_xfer(va - 4, w, *jump_va);
            jumps++;
            break;
        }
        if ((w & 0xFFFF0003u) == 0x00020000u) {       /* RETURN */
            if (s_sub_active) {
                s_sub_active = 0;
                *jump_va = s_sub_return;
                note_xfer(va - 4, w, s_sub_return);
                jumps++;
                break;
            }
            continue;
        }
        /* A method header: bits 31:29 000 (increasing) or 010 (non-increasing).
         * Other top bits used to pass as increasing headers too, so a pointer
         * in data (0x80244950) was decoded as one and the walk went on. */
        if ((w & 0xE0030003u) == 0u || (w & 0xE0030003u) == 0x40000000u) {
            uint32_t count  = (w >> 18) & 0x7FFu;
            s_hdrs[s_hdrs_n % 24u].va = va - 4; s_hdrs[s_hdrs_n % 24u].word = w; s_hdrs_n++;
            uint32_t subch  = (w >> 13) & 7u;
            uint32_t method =  w & 0x1FFCu;
            int noninc = (w & 0xE0000000u) == 0x40000000u;

            uint32_t i;
            for (i = 0; i < count && va != end_va; i++) {
                uint32_t m = noninc ? method : method + i * 4;
                note(subch, m);
                /* Same walk, two consumers: the survey counts, the executor
                 * acts. Keeping them on one decode means they can never
                 * disagree about what the stream said. */
                if (s_exec_enabled)
                    nv2a_pb_exec_method(subch, m,
                                        PB_READ_ARGUMENT(va));
                va += 4;
                words++;
            }
            if (i < count) {                   /* cut off by PUT */
                s_pend.va = va;
                s_pend.count = count - i;
                s_pend.method = noninc ? method : method + i * 4;
                s_pend.subch = subch;
                s_pend.noninc = noninc;
                break;
            }
            if (s_stall_request) {         /* at the packet's end, never inside it */
                s_stall_request = 0;
                s_stalled = 1;
                break;
            }
            continue;
        }
        unknown++;
        {
            /* Not a header, a jump, a call or a return: the walk is no longer
             * on the command stream (misaligned, or in memory that is not a
             * push buffer). Where it happens is the lead. */
            static int told;
            if (told++ < 20)
                fprintf(stderr, "  [PB] not a command: 0x%08X at 0x%08X%s\n", w, va - 4,
                        s_sub_active ? " (in a subroutine)" : "");
            if (told <= 3) {
                unsigned k;
                for (k = 0; k < 24 && k < s_hdrs_n; k++) {
                    unsigned j = (s_hdrs_n - 1 - k) % 24u;
                    fprintf(stderr, "  [PB]   header -%u: 0x%08X at 0x%08X (now 0x%08X)\n", k + 1,
                            s_hdrs[j].word, s_hdrs[j].va, nv2a_pb_read(s_hdrs[j].va));
                }
                fprintf(stderr, "  [PB]   run from 0x%08X to 0x%08X\n",
                        s_runs[(s_runs_n - 1) & 7u].from, s_runs[(s_runs_n - 1) & 7u].put);
                if (told == 1) {
                    /* The 0x100 bytes before the word that was not a command, and a few after. */
                    uint32_t a = va - 4 - 0x100u, j;
                    for (j = 0; j < 72 && a >= XBOX_CONTIG_BASE
                                && a + 72 * 4 < XBOX_CONTIG_BASE + XBOX_CONTIG_WINDOW; j += 8)
                        fprintf(stderr, "  [PB]   %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n",
                                a + j * 4, nv2a_pb_read(a + j * 4), nv2a_pb_read(a + j * 4 + 4),
                                nv2a_pb_read(a + j * 4 + 8), nv2a_pb_read(a + j * 4 + 12),
                                nv2a_pb_read(a + j * 4 + 16), nv2a_pb_read(a + j * 4 + 20),
                                nv2a_pb_read(a + j * 4 + 24), nv2a_pb_read(a + j * 4 + 28));
                }
                for (k = 0; k < 8 && k < s_xfer_n; k++) {
                    unsigned i = (s_xfer_n - 1 - k) & 7u;
                    fprintf(stderr, "  [PB]   transfer -%u: 0x%08X at 0x%08X -> 0x%08X\n",
                            k + 1, s_xfer[i].word, s_xfer[i].from, s_xfer[i].to);
                }
                for (k = 0; k < 8 && k < s_runs_n; k++) {
                    unsigned i = (s_runs_n - 1 - k) & 7u;
                    fprintf(stderr, "  [PB]   run -%u: from 0x%08X toward PUT 0x%08X, stopped 0x%08X\n",
                            k + 1, s_runs[i].from, s_runs[i].put, s_runs[i].stop);
                }
            }
        }
    }

    s_tot_words += words;
    s_tot_unknown += unknown;
    s_tot_jumps += jumps;
    s_tot_segments++;
    return va;
}
