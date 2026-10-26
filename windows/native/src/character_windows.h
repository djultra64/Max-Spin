/* Frozen native athlete session routing. Own work: djultra64. */
#ifndef MAX_SPIN_CHARACTER_WINDOWS_H
#define MAX_SPIN_CHARACTER_WINDOWS_H
#include <wchar.h>
int topspin_windows_character_session_validate(const wchar_t *assets,const wchar_t *session,char digest[65]);
/* host initially contains the original SDK translation; modified only on 1. */
int topspin_windows_character_route(const char *guest,wchar_t *host,unsigned count);
void topspin_windows_character_session_close(void);
#endif
