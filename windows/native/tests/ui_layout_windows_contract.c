/* Real native file/hash/overlay contract against locally prepared RUI files.
 * No original data is included or changed. Own work: djultra64. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ui_layout_windows.h"
#include "ui_layout_resources.h"
#define CHECK(value) do{if(!(value)){fprintf(stderr,"FAIL %s:%d: %s (Win32=%lu)\n",__FILE__,__LINE__,#value,(unsigned long)GetLastError());return 1;}}while(0)
extern BOOL __wrap_xbox_translate_path(const char*,WCHAR*,DWORD);
static WCHAR original[32768];static int translator_ok=1;
/* The SDK path translator is the external boundary. Actual native hash, file
 * access, private selection and refusal behavior all run unchanged below. */
BOOL __real_xbox_translate_path(const char *guest,WCHAR *host,DWORD count)
{(void)guest;if(!translator_ok||wcslen(original)>=count)return FALSE;wcscpy(host,original);return TRUE;}
static int resource_path(WCHAR *out,const WCHAR *root,unsigned i,int patched)
{
 WCHAR leaf[128];if(!MultiByteToWideChar(CP_UTF8,0,topspin_ui_layout_resources[i].path,-1,leaf,128))return 0;
 int n=swprintf(out,32768,L"%ls/%ls%ls",root,patched?L"patches/":L"",leaf);return n>0&&n<32768;
}
static int copy_resource(const WCHAR *source,const WCHAR *out)
{
 WCHAR parent[32768];wcscpy(parent,out);WCHAR *slash=wcsrchr(parent,L'/');if(!slash)return 0;*slash=0;
 /* Shell directory creation requires native separators, unlike CreateFileW. */
 for(WCHAR *p=parent;*p;p++)if(*p==L'/')*p=L'\\';
 int created=SHCreateDirectoryExW(NULL,parent,NULL);if(created!=ERROR_SUCCESS&&created!=ERROR_ALREADY_EXISTS&&created!=ERROR_FILE_EXISTS)return 0;
 SetFileAttributesW(out,FILE_ATTRIBUTE_NORMAL);return CopyFileW(source,out,FALSE)!=0;
}
int wmain(int argc,wchar_t **argv)
{
 CHECK(argc==3);CHECK(topspin_windows_ui_layout_validate(argv[1],argv[2]));
 WCHAR base[32768],cache[32768],source[32768],target[32768],out[32768];
 CHECK(GetTempPathW(32768,base));CHECK(swprintf(cache,32768,L"%lsMaxSpin-ui-contract-%lu-%llu",base,(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64())>0);CHECK(CreateDirectoryW(cache,NULL));
 unsigned count=sizeof topspin_ui_layout_resources/sizeof topspin_ui_layout_resources[0],intro=count;
 for(unsigned i=0;i<count;i++){
  CHECK(resource_path(source,argv[2],i,1)&&resource_path(target,cache,i,1));CHECK(copy_resource(source,target));
  if(!strcmp(topspin_ui_layout_resources[i].path,"Media/Rui/MatchIntro.rui"))intro=i;
 }
 CHECK(intro<count);CHECK(topspin_windows_ui_layout_validate(argv[1],cache));
 CHECK(resource_path(original,argv[1],intro,0));CHECK(resource_path(target,cache,intro,1));
 CHECK(SetEnvironmentVariableW(L"TOPSPIN_UI_LAYOUT_DIR",cache));
 CHECK(__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,32768)&&!wcscmp(out,target));
 CHECK(__wrap_xbox_translate_path("u:\\Media\\Rui\\MatchIntro.rui",out,32768)&&!wcscmp(out,original));
 CHECK(__wrap_xbox_translate_path("d:\\TopSpin(Game).xbe",out,32768)&&!wcscmp(out,original));
 CHECK(!__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,8));
 translator_ok=0;CHECK(!__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,32768));translator_ok=1;
 CHECK(SetFileAttributesW(target,FILE_ATTRIBUTE_NORMAL));
 HANDLE file=CreateFileW(target,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
 unsigned char byte;DWORD got;CHECK(ReadFile(file,&byte,1,&got,NULL)&&got==1);byte^=1;CHECK(SetFilePointer(file,0,NULL,FILE_BEGIN)!=INVALID_SET_FILE_POINTER);CHECK(WriteFile(file,&byte,1,&got,NULL)&&got==1);CHECK(CloseHandle(file));
 CHECK(!topspin_windows_ui_layout_validate(argv[1],cache));
 CHECK(!__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,32768));
 CHECK(resource_path(source,argv[2],intro,1));CHECK(copy_resource(source,target));
 CHECK(topspin_windows_ui_layout_validate(argv[1],cache));
 /* An authentic patched archive cannot impersonate its source original. */
 wcscpy(original,target);CHECK(!__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,32768));
 CHECK(resource_path(original,argv[1],intro,0));
 CHECK(SetEnvironmentVariableW(L"TOPSPIN_UI_LAYOUT_DIR",NULL));
 CHECK(__wrap_xbox_translate_path("d:\\Media\\Rui\\MatchIntro.rui",out,32768)&&!wcscmp(out,original));
 CHECK(topspin_windows_ui_layout_validate(argv[1],argv[2]));
 for(unsigned i=0;i<count;i++){CHECK(resource_path(target,cache,i,1));SetFileAttributesW(target,FILE_ATTRIBUTE_NORMAL);CHECK(DeleteFileW(target));WCHAR *slash;while((slash=wcsrchr(target,L'/'))&&slash>target+wcslen(cache)){*slash=0;RemoveDirectoryW(target);}}
 CHECK(RemoveDirectoryW(cache));
 puts("PASS native Windows UI: all exact hashes, real private archive routing, unchanged saves/executables, bad original/private rejection, bounded output, no admin links, original inputs preserved");return 0;
}
