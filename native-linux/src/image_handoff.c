/* Exact Xbox launch-page transfer between native processes. Own work: djultra64.
 * No payload/header synthesis and no process execution or mode selection. */
#define _POSIX_C_SOURCE 200809L
#include "image_handoff.h"
#include "host_shutdown.h"
#include "kernel/kernel.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *handoff_output;

static void capture_page(uint32_t routine, const void *page, size_t size)
{
    char *temporary;
    int fd = -1, ok = 1;
    size_t sent = 0;
    topspin_host_input_shutdown();
    if (!handoff_output || routine != 2 || !page || size != 4096)
        return;
    temporary = malloc(strlen(handoff_output) + sizeof(".XXXXXX"));
    if (!temporary) {
        fprintf(stderr, "[HANDOFF] cannot allocate output filename\n");
        return;
    }
    sprintf(temporary, "%s.XXXXXX", handoff_output);
    fd = mkstemp(temporary);
    if (fd < 0)
        ok = 0;
    while (ok && sent < size) {
        ssize_t n = write(fd, (const unsigned char *)page + sent, size - sent);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ok = 0;
        else
            sent += (size_t)n;
    }
    if (fd >= 0) {
        if (ok && fsync(fd)) ok = 0;
        if (close(fd)) ok = 0;
    }
    if (ok && rename(temporary, handoff_output)) ok = 0;
    if (!ok) {
        unlink(temporary);
        fprintf(stderr, "[HANDOFF] failed to publish launch page: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "[HANDOFF] saved exact 4096-byte launch page\n");
    }
    free(temporary);
}

int topspin_image_handoff_init(void)
{
    const char *input = getenv("TOPSPIN_LAUNCH_PAGE_IN");
    const char *output = getenv("TOPSPIN_LAUNCH_PAGE_OUT");
    unsigned char page[4096];
    char *new_output = NULL;
    int have_input = input && *input;
    if (have_input) {
        FILE *stream = fopen(input, "rb");
        size_t n;
        int extra, bad;
        if (!stream) {
            fprintf(stderr, "[HANDOFF] cannot open input: %s\n", strerror(errno));
            return 0;
        }
        n = fread(page, 1, sizeof(page), stream);
        extra = fgetc(stream);
        bad = ferror(stream);
        if (fclose(stream)) bad = 1;
        if (n != sizeof(page) || extra != EOF || bad) {
            fprintf(stderr, "[HANDOFF] input must contain exactly 4096 bytes\n");
            return 0;
        }
    }
    if (output && *output) {
        new_output = strdup(output);
        if (!new_output) return 0;
        /* A stale successful record must never be mistaken for this exit.
         * Input was fully copied first, so IN and OUT can name one file. */
        if (unlink(output) && errno != ENOENT) {
            fprintf(stderr, "[HANDOFF] cannot clear stale output: %s\n", strerror(errno));
            free(new_output);
            return 0;
        }
    }
    if (!xbox_SetInitialLaunchDataPage(have_input ? page : NULL,
                                      have_input ? sizeof(page) : 0)) {
        free(new_output);
        return 0;
    }
    free(handoff_output);
    handoff_output = new_output;
    xbox_SetFirmwareReturnCallback(capture_page);
    return 1;
}
