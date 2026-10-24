/* UTF-16 bounded settings IO with exclusive sibling + atomic file replacement.
 * Own work: djultra64. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "graphics_settings.h"
#include "graphics_settings_io.h"
static wchar_t *wide(const char*s){if(!s)return NULL;int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);wchar_t*w=n?malloc((size_t)n*sizeof*w):NULL;if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,n)){free(w);w=NULL;}return w;}
static int fail(char*e,size_t n,const char*s){if(e&&n)snprintf(e,n,"%s (Win32 %lu)",s,(unsigned long)GetLastError());return 0;}
int topspin_windows_read_file(const char*path,char*text,size_t cap,size_t*used){
 wchar_t*w=wide(path);if(!w)return 0;HANDLE f=CreateFileW(w,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);free(w);if(f==INVALID_HANDLE_VALUE)return 0;
 BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size;DWORD got=0;int ok=GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(f,&size)&&size.QuadPart>0&&(uint64_t)size.QuadPart<=cap;
 if(ok)ok=ReadFile(f,text,(DWORD)size.QuadPart,&got,NULL)&&got==(DWORD)size.QuadPart;
 if(!CloseHandle(f))ok=0;if(ok)*used=got;return ok;
}
int topspin_graphics_settings_load(const char*p,TopspinGraphicsSettings*out,char*e,size_t n){char text[TOPSPIN_GRAPHICS_INI_LIMIT+1];size_t used;if(!p||!out||!topspin_windows_read_file(p,text,TOPSPIN_GRAPHICS_INI_LIMIT,&used))return fail(e,n,"Cannot read bounded settings file");return topspin_graphics_settings_parse(text,used,out,e,n);}
int topspin_graphics_settings_store_file(const char*p,const char*text,char*e,size_t n){
 wchar_t*w=wide(p);if(!w||!text){free(w);return fail(e,n,"Invalid settings path");}
 DWORD attrs=GetFileAttributesW(w);if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){free(w);return fail(e,n,"Invalid settings destination");}
 size_t capacity=wcslen(w)+64;wchar_t*temp=calloc(capacity,sizeof*temp);HANDLE f=INVALID_HANDLE_VALUE;static LONG serial;
 if(temp)for(unsigned i=0;i<32;i++){swprintf(temp,capacity,L"%ls.%lu.%lu.tmp",w,(unsigned long)GetCurrentProcessId(),(unsigned long)InterlockedIncrement(&serial));f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,NULL);if(f!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_FILE_EXISTS)break;}
 int ok=f!=INVALID_HANDLE_VALUE;DWORD done=0;size_t len=strlen(text);if(ok)ok=len<=UINT32_MAX&&WriteFile(f,text,(DWORD)len,&done,NULL)&&done==len&&FlushFileBuffers(f);if(f!=INVALID_HANDLE_VALUE&&!CloseHandle(f))ok=0;
 if(ok)ok=MoveFileExW(temp,w,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;else if(f!=INVALID_HANDLE_VALUE)DeleteFileW(temp);
 if(!ok&&f!=INVALID_HANDLE_VALUE)DeleteFileW(temp);free(temp);free(w);if(ok){if(e&&n)e[0]=0;return 1;}return fail(e,n,"Cannot replace settings file");
}
TopspinWindowMode topspin_graphics_session_window_mode(void){const char*p=getenv("TOPSPIN_SESSION_RENDER_INI");if(!p)p=getenv("TOPSPIN_GRAPHICS_INI");TopspinGraphicsSettings s;topspin_graphics_settings_defaults(&s);if(p&&*p)topspin_graphics_settings_load(p,&s,NULL,0);return s.window_mode;}
