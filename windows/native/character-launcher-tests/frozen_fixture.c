/* Test-only stand-in for the frozen frontend, never shipped. djultra64. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "character_command.h"
#include "test_python.h"
int wmain(int argc,wchar_t **argv)
{
 if(argc>2&&!wcscmp(argv[1],L"--tool")){
  const wchar_t *args[16];if(argc>16)return 90;args[0]=TEST_PYTHON;
  for(int i=2;i<argc;i++)args[i-1]=argv[i];
  wchar_t command[32768];if(!topspin_windows_character_command(command,32768,(unsigned)argc-1,args))return 91;
  STARTUPINFOW startup={.cb=sizeof startup};PROCESS_INFORMATION child={0};
  if(!CreateProcessW(TEST_PYTHON,command,NULL,NULL,TRUE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,NULL,NULL,&startup,&child))return 92;
  CloseHandle(child.hThread);if(WaitForSingleObject(child.hProcess,10000)!=WAIT_OBJECT_0)return 93;
  DWORD status=1;GetExitCodeProcess(child.hProcess,&status);CloseHandle(child.hProcess);return (int)status;
 }
 wchar_t report[32768];if(!GetEnvironmentVariableW(L"MAX_SPIN_FIXTURE_REPORT",report,32768))return 94;
 HANDLE file=CreateFileW(report,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);if(file==INVALID_HANDLE_VALUE)return 95;
 const char report_text[]="setup without XBE or Python prompts";
 DWORD n;int ok=argc==2&&!wcscmp(argv[1],L"--setup")&&WriteFile(file,report_text,sizeof report_text-1,&n,NULL)&&n==sizeof report_text-1;CloseHandle(file);return ok?0:96;
}
