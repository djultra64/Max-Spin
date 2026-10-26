/* Native Windows process/filesystem boundary. Own work: djultra64. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "character_launcher_platform.h"
#include "character_command.h"

#define PATH_CAP 32768
#define HELD_CAP 512
struct Held {HANDLE file[HELD_CAP];unsigned count;};
static void release(struct Held *held)
{
 while(held->count)CloseHandle(held->file[--held->count]);
}
/* Pin all components without write/delete sharing; cleanup cannot traverse a
 * junction swapped in after inspection. Paths match the routing drive policy. */
static HANDLE safe_open(const wchar_t *path,DWORD access,struct Held *held)
{
 wchar_t part[PATH_CAP];DWORD n=GetFullPathNameW(path,PATH_CAP,part,NULL);
 if(!n||n>=PATH_CAP||!((part[0]>=L'A'&&part[0]<=L'Z')||(part[0]>=L'a'&&part[0]<=L'z'))||part[1]!=L':'){SetLastError(ERROR_INVALID_NAME);return INVALID_HANDLE_VALUE;}
 for(DWORD i=0;i<n;i++)if(part[i]==L'\\')part[i]=L'/';
 if(part[2]!=L'/'){SetLastError(ERROR_INVALID_NAME);return INVALID_HANDLE_VALUE;}
 while(n>3&&part[n-1]==L'/')part[--n]=0;
 for(DWORD i=3;i<=n;i++)if(!part[i]||part[i]==L'/'){
  if(held->count==HELD_CAP){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return INVALID_HANDLE_VALUE;}
  wchar_t saved=part[i];part[i]=0;
  HANDLE file=CreateFileW(part,GENERIC_READ|(i==n?access:0),FILE_SHARE_READ,NULL,OPEN_EXISTING,
   FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
  part[i]=saved;if(file==INVALID_HANDLE_VALUE)return file;
  BY_HANDLE_FILE_INFORMATION info;
  int ok=GetFileType(file)==FILE_TYPE_DISK&&GetFileInformationByHandle(file,&info)&&
   !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&(i==n||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY));
  if(!ok){CloseHandle(file);SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
  held->file[held->count++]=file;if(i==n)return file;
 }
 SetLastError(ERROR_INVALID_NAME);return INVALID_HANDLE_VALUE;
}
int topspin_character_platform_kind(const wchar_t *path)
{
 struct Held held={0};HANDLE file=safe_open(path,0,&held);int kind=-1;
 if(file==INVALID_HANDLE_VALUE){DWORD error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)kind=0;}
 else{BY_HANDLE_FILE_INFORMATION info;if(GetFileInformationByHandle(file,&info))kind=info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY?2:1;}
 release(&held);return kind;
}
int topspin_character_platform_packages(const wchar_t *path)
{
 int kind=topspin_character_platform_kind(path);if(!kind)return 0;if(kind!=2)return -1;
 struct Held held={0};if(safe_open(path,0,&held)==INVALID_HANDLE_VALUE){release(&held);return -1;}
 wchar_t probe[PATH_CAP];int n=swprintf(probe,PATH_CAP,L"%ls/*",path);int found=0;
 if(n<=0||n>=PATH_CAP){release(&held);return -1;}
 WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(probe,&data);
 if(search==INVALID_HANDLE_VALUE){DWORD error=GetLastError();release(&held);return error==ERROR_FILE_NOT_FOUND?0:-1;}
 do{
  const wchar_t *p=data.cFileName;while(*p>=L'0'&&*p<=L'9')p++;
  if(p==data.cFileName||*p||!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
  n=swprintf(probe,PATH_CAP,L"%ls/%ls/manifest.json",path,data.cFileName);
  if(n<=0||n>=PATH_CAP){found=-1;break;}
  DWORD attributes=GetFileAttributesW(probe);
  if(attributes!=INVALID_FILE_ATTRIBUTES){found=1;break;}
 }while(FindNextFileW(search,&data));
 FindClose(search);release(&held);return found;
}
int topspin_character_platform_python(wchar_t *path,unsigned cap,int *use_py)
{
 *use_py=0;DWORD n=GetEnvironmentVariableW(L"MAX_SPIN_PYTHON",path,cap);
 if(n)return n<cap&&topspin_character_platform_kind(path)==1;
 n=SearchPathW(NULL,L"py.exe",NULL,cap,path,NULL);
 if(n&&n<cap&&topspin_character_platform_kind(path)==1){*use_py=1;return 1;}
 n=SearchPathW(NULL,L"python.exe",NULL,cap,path,NULL);
 return n&&n<cap&&topspin_character_platform_kind(path)==1;
}
int topspin_character_platform_environment(const wchar_t *key,const wchar_t *value)
{
 return SetEnvironmentVariableW(key,value)!=0||(!value&&GetLastError()==ERROR_ENVVAR_NOT_FOUND);
}
int topspin_character_platform_run(const wchar_t *exe,unsigned count,const wchar_t *const *args,
 const wchar_t *cwd,wchar_t *message,unsigned cap)
{
 /* Drain the entire pipe while retaining at most 16 KiB: noisy helpers cannot
  * block on a full pipe or grow memory/logs without a bound. */
 wchar_t *command=malloc(PATH_CAP*sizeof *command);HANDLE read_pipe=NULL,write_pipe=NULL,input=INVALID_HANDLE_VALUE;
 SECURITY_ATTRIBUTES security={sizeof security,NULL,TRUE};PROCESS_INFORMATION child={0};
 int ok=0;DWORD status=1;char diagnostic[16384];unsigned used=0;
 if(!command||!topspin_windows_character_command(command,PATH_CAP,count,args)||
    !CreatePipe(&read_pipe,&write_pipe,&security,0)||!SetHandleInformation(read_pipe,HANDLE_FLAG_INHERIT,0))goto done;
 input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(input==INVALID_HANDLE_VALUE)goto done;
 STARTUPINFOW startup={.cb=sizeof startup,.dwFlags=STARTF_USESTDHANDLES,
  .hStdInput=input,.hStdOutput=write_pipe,.hStdError=write_pipe};
 if(!CreateProcessW(exe,command,NULL,NULL,TRUE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,NULL,cwd,&startup,&child))goto done;
 CloseHandle(write_pipe);write_pipe=NULL;CloseHandle(child.hThread);child.hThread=NULL;
 char bytes[2048];DWORD got;
 while(ReadFile(read_pipe,bytes,sizeof bytes,&got,NULL)&&got){
  unsigned retain=got;if(retain>sizeof diagnostic-1-used)retain=sizeof diagnostic-1-used;
  memcpy(diagnostic+used,bytes,retain);used+=retain;
 }
 diagnostic[used]=0;
 ok=WaitForSingleObject(child.hProcess,INFINITE)==WAIT_OBJECT_0&&GetExitCodeProcess(child.hProcess,&status)&&status==0;
 if(cap){
  message[0]=0;
  if(used){
   UINT encoding=CP_UTF8;int needed=MultiByteToWideChar(encoding,MB_ERR_INVALID_CHARS,diagnostic,(int)used,NULL,0);
   if(!needed){encoding=CP_ACP;needed=MultiByteToWideChar(encoding,0,diagnostic,(int)used,NULL,0);}
   if(needed>0){wchar_t *wide=malloc(((size_t)needed+1)*sizeof *wide);if(wide){
    int converted=MultiByteToWideChar(encoding,0,diagnostic,(int)used,wide,needed);
    if(converted>0){unsigned n=(unsigned)converted;if(n>=cap)n=cap-1;memcpy(message,wide,n*sizeof *message);message[n]=0;}
    free(wide);
   }}
  }
 }
done:
 if(child.hThread)CloseHandle(child.hThread);if(child.hProcess)CloseHandle(child.hProcess);
 if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
 if(read_pipe)CloseHandle(read_pipe);if(write_pipe)CloseHandle(write_pipe);free(command);return ok;
}
static int erase(HANDLE file,int directory)
{
 BY_HANDLE_FILE_INFORMATION info;
 if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
    !!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory)return 0;
 FILE_DISPOSITION_INFO disposition={TRUE};return SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof disposition)!=0;
}
/* Parent handles remain pinned until child deletions finish. Delete only the
 * bounded protocol members by verified handle, never recurse unknown files. */
void topspin_character_platform_remove(const wchar_t *owned)
{
 struct Held root={0};HANDLE directory=safe_open(owned,DELETE,&root);
 if(directory==INVALID_HANDLE_VALUE){release(&root);return;}
 wchar_t path[PATH_CAP];int n=swprintf(path,PATH_CAP,L"%ls/files",owned);
 if(n>0&&n<PATH_CAP){
  /* Root is already pinned with DELETE access; reopening it without delete
   * sharing would conflict. Open direct children under this pinned path. */
  HANDLE files=CreateFileW(path,GENERIC_READ|DELETE,FILE_SHARE_READ,NULL,OPEN_EXISTING,
   FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
  BY_HANDLE_FILE_INFORMATION info;
  if(files!=INVALID_HANDLE_VALUE){
   if(GetFileInformationByHandle(files,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
    for(unsigned i=0;i<96;i++){
     n=swprintf(path,PATH_CAP,L"%ls/files/%04u.wad",owned,i);if(n<=0||n>=PATH_CAP)break;
     HANDLE file=CreateFileW(path,GENERIC_READ|DELETE,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
     if(file!=INVALID_HANDLE_VALUE){(void)erase(file,0);CloseHandle(file);}
    }
    (void)erase(files,1);
   }
   CloseHandle(files);
  }
 }
 n=swprintf(path,PATH_CAP,L"%ls/manifest.bin",owned);
 if(n>0&&n<PATH_CAP){HANDLE file=CreateFileW(path,GENERIC_READ|DELETE,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
  if(file!=INVALID_HANDLE_VALUE){(void)erase(file,0);CloseHandle(file);}}
 (void)erase(directory,1);release(&root);
}
