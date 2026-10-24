/* Native Win32 launch-page transport. Own work: djultra64.
 * Same original firmware callback contract as the POSIX host: exact 4096 bytes,
 * input copied before stale output removal, exclusive sibling + atomic replace.
 * No process execution, image choice, payload synthesis or save-game handling. */
#include "image_handoff.h"
#include "host_shutdown.h"
#include "kernel/kernel.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t *handoff_output;
static LONG temporary_serial;

/* Absent/empty is optional. A present value must fit the native path bound. */
static int environment_path(const wchar_t *name,wchar_t **out)
{
    *out=NULL;SetLastError(ERROR_SUCCESS);
    DWORD count=GetEnvironmentVariableW(name,NULL,0);
    if(!count)return GetLastError()==ERROR_ENVVAR_NOT_FOUND || GetLastError()==ERROR_SUCCESS;
    if(count>32767)return 0;
    wchar_t *raw=calloc(count,sizeof *raw);
    if(!raw)return 0;
    DWORD got=GetEnvironmentVariableW(name,raw,count);
    if(!got || got>=count){free(raw);return 0;}
    DWORD full_count=GetFullPathNameW(raw,0,NULL,NULL);
    if(!full_count || full_count>32767){free(raw);return 0;}
    wchar_t *full=calloc(full_count,sizeof *full);
    if(!full){free(raw);return 0;}
    got=GetFullPathNameW(raw,full_count,full,NULL);free(raw);
    if(!got || got>=full_count){free(full);return 0;}
    *out=full;return 1;
}
static int absent_error(DWORD error)
{return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;}
static void capture_page(uint32_t routine,const void *page,size_t size)
{
    topspin_host_input_shutdown();
    if(!handoff_output || routine!=2 || !page || size!=4096)return;
    size_t capacity=wcslen(handoff_output)+64;
    wchar_t *temporary=calloc(capacity,sizeof *temporary);
    if(!temporary){fputs("[HANDOFF] cannot allocate output filename\n",stderr);return;}
    HANDLE file=INVALID_HANDLE_VALUE;
    DWORD error=ERROR_SUCCESS;
    int ok=0;
    for(unsigned retry=0;retry<32;retry++){
        LONG serial=InterlockedIncrement(&temporary_serial);
        int n=swprintf(temporary,capacity,L"%ls.%08lx.%08lx.tmp",handoff_output,
            (unsigned long)GetCurrentProcessId(),(unsigned long)(uint32_t)serial);
        if(n<0 || (size_t)n>=capacity){error=ERROR_BUFFER_OVERFLOW;break;}
        file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(file!=INVALID_HANDLE_VALUE)break;
        error=GetLastError();if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS)break;
    }
    if(file!=INVALID_HANDLE_VALUE){
        DWORD sent=0;ok=1;
        while(ok && sent<4096){
            DWORD n=0;
            if(!WriteFile(file,(const unsigned char*)page+sent,4096-sent,&n,NULL) || !n){error=GetLastError();ok=0;}
            else sent+=n;
        }
        if(ok && !FlushFileBuffers(file)){error=GetLastError();ok=0;}
        if(!CloseHandle(file)){error=GetLastError();ok=0;}
        if(ok && !MoveFileExW(temporary,handoff_output,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            error=GetLastError();ok=0;
        }
        /* Delete only the sibling this invocation successfully created. */
        if(!ok)DeleteFileW(temporary);
    }
    if(ok)fputs("[HANDOFF] saved exact 4096-byte launch page\n",stderr);
    else fprintf(stderr,"[HANDOFF] failed to publish launch page: Win32 %lu\n",(unsigned long)error);
    free(temporary);
}
int topspin_image_handoff_init(void)
{
    wchar_t *input=NULL,*output=NULL;
    unsigned char page[4096];
    int ok=0;
    if(!environment_path(L"TOPSPIN_LAUNCH_PAGE_IN",&input) ||
       !environment_path(L"TOPSPIN_LAUNCH_PAGE_OUT",&output))goto done;
    if(input){
        HANDLE file=CreateFileW(input,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(file==INVALID_HANDLE_VALUE)goto done;
        BY_HANDLE_FILE_INFORMATION info;
        LARGE_INTEGER bytes;
        DWORD received=0;
        int valid=GetFileInformationByHandle(file,&info) &&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
            GetFileSizeEx(file,&bytes) && bytes.QuadPart==4096;
        while(valid && received<4096){
            DWORD n=0;
            if(!ReadFile(file,page+received,4096-received,&n,NULL) || !n)valid=0;
            else received+=n;
        }
        if(!CloseHandle(file))valid=0;
        if(!valid)goto done;
    }
    if(output){
        DWORD attributes=GetFileAttributesW(output);
        if(attributes!=INVALID_FILE_ATTRIBUTES){
            if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))goto done;
            if(!DeleteFileW(output))goto done;
        }else if(!absent_error(GetLastError()))goto done;
    }
    if(!xbox_SetInitialLaunchDataPage(input?page:NULL,input?sizeof page:0))goto done;
    free(handoff_output);handoff_output=output;output=NULL;
    xbox_SetFirmwareReturnCallback(capture_page);ok=1;
done:
    if(!ok)fputs("[HANDOFF] invalid input/output or launch-page preparation failed\n",stderr);
    free(input);free(output);return ok;
}
