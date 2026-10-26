/* Win32 CRT argument quoting, without a shell. Own work: djultra64. */
#ifndef MAXSPIN_CHARACTER_COMMAND_H
#define MAXSPIN_CHARACTER_COMMAND_H
#include <stddef.h>
#include <wchar.h>
int topspin_windows_character_command(wchar_t *out,size_t capacity,unsigned count,const wchar_t *const *arguments);
#endif
