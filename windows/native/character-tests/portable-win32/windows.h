/* Only the unrelated RUI Win32 boundary is unavailable in portable tests.
 * Abort if a character route accidentally enters that boundary. djultra64. */
#ifndef CHARACTER_CONTRACT_WINDOWS_H
#define CHARACTER_CONTRACT_WINDOWS_H
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
typedef int BOOL;typedef wchar_t WCHAR;typedef uint32_t DWORD;typedef void *HANDLE;
typedef struct {DWORD dwFileAttributes;} BY_HANDLE_FILE_INFORMATION;
#define TRUE 1
#define FALSE 0
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
#define INVALID_FILE_ATTRIBUTES UINT32_MAX
#define FILE_ATTRIBUTE_REPARSE_POINT 1024
#define FILE_ATTRIBUTE_DIRECTORY 16
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
#define UNAVAILABLE(type,name,args) static inline type name args {abort();}
UNAVAILABLE(int,MultiByteToWideChar,(unsigned a,DWORD b,const char *c,int d,wchar_t *e,int f))
UNAVAILABLE(DWORD,GetFileAttributesW,(const wchar_t *p))
UNAVAILABLE(HANDLE,CreateFileW,(const wchar_t *p,DWORD a,DWORD b,void *c,DWORD d,DWORD e,HANDLE f))
UNAVAILABLE(BOOL,GetFileInformationByHandle,(HANDLE a,BY_HANDLE_FILE_INFORMATION *b))
UNAVAILABLE(BOOL,ReadFile,(HANDLE a,void *b,DWORD c,DWORD *d,void *e))
UNAVAILABLE(BOOL,CloseHandle,(HANDLE a))
UNAVAILABLE(DWORD,GetEnvironmentVariableW,(const wchar_t *p,wchar_t *q,DWORD n))
#endif
