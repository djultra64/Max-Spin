/* Prompt artwork preference, separate from action mappings. djultra64. */
#ifndef TOPSPIN_CONTROLLER_FAMILY_SESSION_H
#define TOPSPIN_CONTROLLER_FAMILY_SESSION_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit session file is authoritative; invalid/missing file is an error.
 * Without SESSION_FILE, resolve existing CONTROLLER_FAMILY (default AUTO).
 * Read only; does not require the input backend to have started. */
int topspin_controller_family_load(int *family,char *error,size_t size);
/* Menu event/render serialized caller. Prepare private temp before apply;
 * rename commits, a precommit failure preserves file and rolls backend back.
 * apply must accept previous if it accepted requested. No action maps change.
 * POSIX adapter only; native Windows requires its own verified replacement. */
int topspin_controller_family_change(int previous,int requested,
                                    int (*apply)(int),char *error,size_t size);
#ifdef __cplusplus
}
#endif
#endif
