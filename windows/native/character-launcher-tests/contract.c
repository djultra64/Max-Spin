/* Native supervisor/helper boundary, no game/window/GPU. djultra64. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include "character_command.h"
#include "test_python.h"
extern int topspin_windows_characters_begin(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,wchar_t*,unsigned);
extern void topspin_windows_characters_end(const wchar_t*);
#ifdef MISSING_IMPLEMENTATION
int topspin_windows_characters_begin(const wchar_t*a,const wchar_t*b,const wchar_t*c,const wchar_t*d,wchar_t*e,unsigned f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
void topspin_windows_characters_end(const wchar_t*p){(void)p;}
#endif
/* Routing validation is independently exercised by character-tests; this
 * boundary double checks that the actual helper completed before activation. */
int topspin_windows_character_session_validate(const wchar_t*assets,const wchar_t*session,char digest[65])
{
 (void)assets;wchar_t p[32768];swprintf(p,32768,L"%ls/manifest.bin",session);
 HANDLE f=CreateFileW(p,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(f==INVALID_HANDLE_VALUE)return 0;
 unsigned char bytes[12];DWORD n=0;int ok=ReadFile(f,bytes,12,&n,NULL)&&n==12&&!memcmp(bytes,"MSCHAR1\0\0\0\0\0",12);CloseHandle(f);
 memset(digest,'0',64);digest[64]=0;return ok;
}
void topspin_windows_character_session_close(void){}
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAILED line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static void join(wchar_t*out,const wchar_t*a,const wchar_t*b){swprintf(out,32768,L"%ls/%ls",a,b);}
static int write_file(const wchar_t*p,const char*s)
{
 HANDLE f=CreateFileW(p,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);if(f==INVALID_HANDLE_VALUE)return 0;
 DWORD n=0;int ok=WriteFile(f,s,(DWORD)strlen(s),&n,NULL)&&n==strlen(s);CloseHandle(f);return ok;
}
int main(void)
{
 wchar_t temporary[32768],root[32768],assets[32768],packages[32768],session[32768],p[32768],q[32768],message[2048],value[32768];
 CHECK(GetTempPathW(32768,temporary));swprintf(root,32768,L"%lsMaxSpin parity \u00f1 %lu",temporary,(unsigned long)GetCurrentProcessId());CHECK(CreateDirectoryW(root,NULL));
 join(assets,root,L"assets con espacios");join(packages,root,L"characters");join(session,root,L"session");CHECK(CreateDirectoryW(assets,NULL));CHECK(CreateDirectoryW(packages,NULL));CHECK(CreateDirectoryW(session,NULL));
 SetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_SESSION",L"stale");SetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_MANIFEST_SHA256",L"stale");SetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_ASSETS",L"stale");
 CHECK(topspin_windows_characters_begin(root,assets,packages,session,message,2048));
 CHECK(!GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_SESSION",value,32768));CHECK(!GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_MANIFEST_SHA256",value,32768));CHECK(!GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_ASSETS",value,32768));
 join(p,packages,L"9");CHECK(CreateDirectoryW(p,NULL));join(q,p,L"manifest.json");CHECK(write_file(q,"{}"));
 SetEnvironmentVariableW(L"MAX_SPIN_PYTHON",L"Z:/missing/python.exe");CHECK(!topspin_windows_characters_begin(root,assets,packages,session,message,2048));CHECK(message[0]);
 join(p,root,L"native-linux");CHECK(CreateDirectoryW(p,NULL));join(q,p,L"tools");CHECK(CreateDirectoryW(q,NULL));join(p,q,L"character_session.py");
 CHECK(write_file(p,"import sys,struct\nfrom pathlib import Path\np=Path(sys.argv[sys.argv.index('--output')+1]);p.mkdir();(p/'files').mkdir()\n(p/'manifest.bin').write_bytes(b'MSCHAR1\\0'+struct.pack('<I',0))\n(p/'arguments.txt').write_bytes('\\n'.join(sys.argv).encode('utf-8'))\n"));
 SetEnvironmentVariableW(L"MAX_SPIN_PYTHON",TEST_PYTHON);CHECK(topspin_windows_characters_begin(root,assets,packages,session,message,2048));
 CHECK(GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_SESSION",value,32768));join(q,session,L"characters");CHECK(!wcscmp(value,q));
 CHECK(GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_ASSETS",value,32768)&&!wcscmp(value,assets));CHECK(GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_MANIFEST_SHA256",value,32768)==64);
 join(q,session,L"characters/arguments.txt");HANDLE f=CreateFileW(q,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f!=INVALID_HANDLE_VALUE);char text[8192]={0};DWORD n=0;CHECK(ReadFile(f,text,sizeof text-1,&n,NULL));CloseHandle(f);CHECK(strstr(text,"--assets\n")&&strstr(text,"--characters\n")&&strstr(text,"assets con espacios"));CHECK(DeleteFileW(q));
 /* A CPU-only child inherits the frozen route and verifies it throughout its
  * lifetime. The owner retains the session until termination is confirmed. */
 const wchar_t *worker_args[]={TEST_PYTHON,L"-c",L"import os,time; from pathlib import Path; p=Path(os.environ['MAX_SPIN_CHARACTER_SESSION'])/'manifest.bin'; assert p.is_file(); time.sleep(0.2); assert p.is_file(); assert len(os.environ['MAX_SPIN_CHARACTER_MANIFEST_SHA256'])==64"};
 wchar_t worker_command[4096];CHECK(topspin_windows_character_command(worker_command,4096,3,worker_args));
 STARTUPINFOW startup={.cb=sizeof startup};PROCESS_INFORMATION worker={0};CHECK(CreateProcessW(TEST_PYTHON,worker_command,NULL,NULL,FALSE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,NULL,root,&startup,&worker));CloseHandle(worker.hThread);
 join(q,session,L"characters/manifest.bin");CHECK(GetFileAttributesW(q)!=INVALID_FILE_ATTRIBUTES);
 CHECK(WaitForSingleObject(worker.hProcess,10000)==WAIT_OBJECT_0);DWORD worker_status=1;CHECK(GetExitCodeProcess(worker.hProcess,&worker_status)&&worker_status==0);CloseHandle(worker.hProcess);
 topspin_windows_characters_end(session);join(q,session,L"characters");CHECK(GetFileAttributesW(q)==INVALID_FILE_ATTRIBUTES);CHECK(!GetEnvironmentVariableW(L"MAX_SPIN_CHARACTER_SESSION",value,32768));
 CHECK(DeleteFileW(p));join(p,root,L"native-linux/tools");CHECK(RemoveDirectoryW(p));join(p,root,L"native-linux");CHECK(RemoveDirectoryW(p));join(p,packages,L"9/manifest.json");CHECK(DeleteFileW(p));join(p,packages,L"9");CHECK(RemoveDirectoryW(p));CHECK(RemoveDirectoryW(packages));CHECK(RemoveDirectoryW(assets));CHECK(RemoveDirectoryW(session));CHECK(RemoveDirectoryW(root));
 puts("PASS native launcher/helper: empty defaults, stale environment, missing Python, Unicode/spaces, validated activation and owned cleanup");return 0;
}
