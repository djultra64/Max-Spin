/* OS boundary for the portable supervisor flow. Own work: djultra64. */
#ifndef MAXSPIN_CHARACTER_LAUNCHER_PLATFORM_H
#define MAXSPIN_CHARACTER_LAUNCHER_PLATFORM_H
#include <wchar.h>
/* kind: 0 absent, 1 regular file, 2 directory, -1 unsafe/error. */
int topspin_character_platform_kind(const wchar_t *path);
int topspin_character_platform_packages(const wchar_t *path);
int topspin_character_platform_python(wchar_t *path,unsigned capacity,int *use_py);
int topspin_character_platform_environment(const wchar_t *key,const wchar_t *value);
int topspin_character_platform_run(const wchar_t *exe,unsigned count,const wchar_t *const *args,
 const wchar_t *cwd,wchar_t *message,unsigned capacity);
void topspin_character_platform_remove(const wchar_t *owned);
#endif
