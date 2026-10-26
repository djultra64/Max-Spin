/* Persistent frozen athlete preparation. Own work: djultra64. */
#ifndef MAXSPIN_CHARACTER_LAUNCHER_H
#define MAXSPIN_CHARACTER_LAUNCHER_H
#include <wchar.h>
int topspin_windows_characters_begin(const wchar_t *install,const wchar_t *assets,
 const wchar_t *packages,const wchar_t *session,wchar_t *message,unsigned capacity);
void topspin_windows_characters_end(const wchar_t *session);
#endif
