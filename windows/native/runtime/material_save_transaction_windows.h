/* Native Windows transaction hooks. Own work: djultra64. */
#ifndef MAXSPIN_MATERIAL_SAVE_TRANSACTION_WINDOWS_H
#define MAXSPIN_MATERIAL_SAVE_TRANSACTION_WINDOWS_H
#include <windows.h>
#include <stddef.h>
int xbox_save_path_transaction_windows(WCHAR *path,size_t capacity);
int xbox_save_mutation_transaction_windows(HANDLE file,const WCHAR *path);
int xbox_save_opened_transaction_windows(HANDLE file,const WCHAR *path);
void xbox_save_closed_transaction_windows(HANDLE file);
void xbox_save_failed_transaction_windows(void);
#endif
