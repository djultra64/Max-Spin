/* Linux private save transaction. Own work: djultra64. */
#ifndef TOPSPIN_MATERIAL_SAVE_TRANSACTION_H
#define TOPSPIN_MATERIAL_SAVE_TRANSACTION_H
#include <stddef.h>
/* Configure once before guest startup. Normal capability1 admits the canonical
 * owned save root passed to this image; legacy diagnostics stay reports-only. */
int mc_save_transaction_configure(const char *save_root);
int mc_save_transaction_begin(void);
/* Commit only on a complete original Save and coherent sources. Failed saves
 * retain the pre-save directory; failed stage directories remain as evidence. */
int mc_save_transaction_end(int success);
int mc_save_transaction_failed(void);
/* Requires SDK SaveTransactionIOVersion2: opens and attribute queries share
 * one namespace. U title writes use the staged title, Z:\\TmpStorage uses
 * a separate private workspace; original title/cache are never modified.
 * Optional SDK hooks, called for POSIX object paths and mutations. */
int xbox_save_path_transaction(char *path, size_t capacity);
int xbox_save_mutation_transaction(int fd, const char *path);
#endif
