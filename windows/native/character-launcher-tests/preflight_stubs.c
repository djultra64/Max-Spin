/* If this preflight-only process reaches game preparation, fail immediately.
 * The launcher and all Win32 setup/process/path boundaries remain real. djultra64. */
#include <stdlib.h>
#include <wchar.h>
int topspin_windows_ui_layout_validate(const wchar_t*a,const wchar_t*b){(void)a;(void)b;abort();}
int topspin_windows_characters_begin(const wchar_t*a,const wchar_t*b,const wchar_t*c,const wchar_t*d,wchar_t*e,unsigned f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();}
void topspin_windows_characters_end(const wchar_t*p){(void)p;abort();}
