/* Authenticated private RUI archive routing; no links or disc copies. djultra64. */
#define MAX_SPIN_HOST_WINDOWS_IMPLEMENTATION
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "ui_layout_windows.h"
#include "character_windows.h"
#include "ui_layout_resources.h"
extern int topspin_ui_layout_disc_index(const char*);
#define RESOURCE_COUNT (sizeof topspin_ui_layout_resources/sizeof topspin_ui_layout_resources[0])
static int joined(wchar_t *out,size_t count,const wchar_t *root,const char *relative,int patched)
{
 wchar_t leaf[128];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,-1,leaf,128))return 0;
 int n=swprintf(out,count,L"%ls/%ls%ls",root,patched?L"patches/":L"",leaf);
 return n>0&&(size_t)n<count;
}
static int plain_path(const wchar_t *path)
{
 /* Reject reparse points at every component, including cache parents. */
 wchar_t part[32768];size_t n=wcslen(path);if(!n||n>=32768)return 0;memcpy(part,path,(n+1)*sizeof *part);
 for(size_t i=0;i<=n;i++)if(!part[i]||part[i]==L'/'||part[i]==L'\\'){
  if(i<3)continue;wchar_t saved=part[i];part[i]=0;DWORD flags=GetFileAttributesW(part);part[i]=saved;
  if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_REPARSE_POINT))return 0;
 }
 return 1;
}
static int matches(const wchar_t *path,const char *expected)
{
 if(!plain_path(path))return 0;
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;
 BY_HANDLE_FILE_INFORMATION info;HCRYPTPROV provider=0;HCRYPTHASH hash=0;unsigned char bytes[65536],digest[32];DWORD got=0,n=32;int ok=0;
 if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
    ||!CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)
    ||!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))goto done;
 for(;;){if(!ReadFile(file,bytes,sizeof bytes,&got,NULL))goto done;if(!got)break;if(!CryptHashData(hash,bytes,got,0))goto done;}
 if(CryptGetHashParam(hash,HP_HASHVAL,digest,&n,0)&&n==32){
  static const char hex[]="0123456789abcdef";char text[65];
  for(unsigned i=0;i<32;i++){text[i*2]=hex[digest[i]>>4];text[i*2+1]=hex[digest[i]&15];}text[64]=0;ok=!strcmp(text,expected);
 }
done:
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(file);return ok;
}
int topspin_windows_ui_layout_validate(const wchar_t *assets,const wchar_t *cache)
{
 wchar_t original[32768],patched[32768];
 for(unsigned i=0;i<RESOURCE_COUNT;i++){
  const struct TopspinUILayoutResource *resource=&topspin_ui_layout_resources[i];
  if(!joined(original,32768,assets,resource->path,0)||!joined(patched,32768,cache,resource->path,1)
     ||!matches(original,resource->original)||!matches(patched,resource->patched)){
   fprintf(stderr,"[UI-LAYOUT] Original or private archive does not match: %s\n",resource->path);return 0;
  }
 }
 return 1;
}
#ifndef TOPSPIN_UI_LAYOUT_LAUNCHER
extern BOOL __real_xbox_translate_path(const char*,WCHAR*,DWORD);
BOOL __wrap_xbox_translate_path(const char *guest,WCHAR *host,DWORD count)
{
 if(!__real_xbox_translate_path(guest,host,count))return FALSE;
 int character=topspin_windows_character_route(guest,host,count);
 if(character)return character>0?TRUE:FALSE;
 int index=topspin_ui_layout_disc_index(guest);if(index<0)return TRUE;
 wchar_t cache[32768],patched[32768];DWORD n=GetEnvironmentVariableW(L"TOPSPIN_UI_LAYOUT_DIR",cache,32768);
 const struct TopspinUILayoutResource *resource=&topspin_ui_layout_resources[index];
 /* Direct workers without an overlay preserve their original diagnostic path. */
 if(!n)return TRUE;
 if(n>=32768||!joined(patched,32768,cache,resource->path,1)||wcslen(patched)>=count
    ||!matches(host,resource->original)||!matches(patched,resource->patched)){
  fprintf(stderr,"[UI-LAYOUT] Refusing changed original/private archive: %s\n",resource->path);return FALSE;
 }
 wcscpy(host,patched);return TRUE;
}
#endif
