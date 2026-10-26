/* Actual SDK file I/O against synthetic routed bytes. Own work: djultra64. */
#include "kernel/kernel.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

extern int __wrap_xbox_translate_path(const char*,wchar_t*,DWORD);
void xbox_log(int level,const char *category,const char *format,...)
{(void)level;(void)category;(void)format;}

/* Only the original disc-root lookup is supplied by the surrounding fixture;
 * the production routing wrapper and SDK NtCreateFile/NtReadFile run intact. */
BOOL xbox_translate_path(const char *guest,xbox_host_char *host,DWORD count)
{
#ifdef _WIN32
 return __wrap_xbox_translate_path(guest,host,count);
#else
 wchar_t wide[32768];
 if(!__wrap_xbox_translate_path(guest,wide,32768))return FALSE;
 size_t n=wcstombs(host,wide,count);return n!=(size_t)-1&&n<count;
#endif
}

int character_sdk_read(const char *guest,const char *expected,unsigned length)
{
 XBOX_ANSI_STRING name={(USHORT)strlen(guest),(USHORT)(strlen(guest)+1),(char*)guest};
 XBOX_OBJECT_ATTRIBUTES attributes={NULL,&name,0};XBOX_IO_STATUS_BLOCK ios={0};
 /* Menu disc archive path: _open(path,0x8000) -> _sopen(...,_SH_DENYNO).
  * Guest0x10C386 sets ECX0x40;0x10C14E maps that to share3;0x1664F adds
  * SYNCHRONIZE|FILE_READ_ATTRIBUTES. Rooted disassembly evidence is recorded
  * in title-file-open-evidence.md. Also exercise the title's share1 reader. */
 const ULONG shares[]={3,1};
 for(unsigned i=0;i<2;i++){
  HANDLE file=NULL;unsigned char bytes[256];LARGE_INTEGER offset={0};
  if(length>sizeof bytes)return 0;
  if(xbox_NtCreateFile(&file,0x80100080u,&attributes,&ios,NULL,0x80,shares[i],XBOX_FILE_OPEN,0x60)!=STATUS_SUCCESS)return 0;
  NTSTATUS status=xbox_NtReadFile(file,NULL,NULL,NULL,&ios,bytes,length,&offset);
  int ok=status==STATUS_SUCCESS&&ios.Information==length&&!memcmp(bytes,expected,length);
  if(xbox_NtClose(file)!=STATUS_SUCCESS||!ok)return 0;
 }
 return 1;
}
