/* Real Win32 file I/O, with only firmware/input callbacks controlled. djultra64. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "image_handoff.h"
static void (*callback)(uint32_t,const void*,size_t);
static unsigned char initial[4096];
static size_t initial_size;
static unsigned shutdowns;
int xbox_SetInitialLaunchDataPage(const void *page,size_t bytes){
    if(bytes!=0 && bytes!=sizeof initial)return 0;
    if(bytes)memcpy(initial,page,bytes);initial_size=bytes;return 1;
}
void xbox_SetFirmwareReturnCallback(void (*fn)(uint32_t,const void*,size_t)){callback=fn;}
void topspin_host_input_shutdown(void){shutdowns++;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s (Win32 %lu)\n",__LINE__,#x,GetLastError());return 1;}}while(0)
static int write_bytes(const wchar_t *path,const void *bytes,DWORD count){
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD n=0; if(f==INVALID_HANDLE_VALUE)return 0;
    int ok=WriteFile(f,bytes,count,&n,NULL)&&n==count;return CloseHandle(f)&&ok;
}
static int read_exact(const wchar_t *path,unsigned char *bytes){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    DWORD n=0;LARGE_INTEGER size={0};if(f==INVALID_HANDLE_VALUE)return 0;
    int ok=GetFileSizeEx(f,&size)&&size.QuadPart==4096&&ReadFile(f,bytes,4096,&n,NULL)&&n==4096;
    return CloseHandle(f)&&ok;
}
int main(void){
    wchar_t temp[MAX_PATH],dir[MAX_PATH],input[MAX_PATH],output[MAX_PATH],invalid[MAX_PATH];
    unsigned char page[4097],got[4096];
    for(unsigned i=0;i<sizeof page;i++)page[i]=(unsigned char)(i*73u+19u);
    CHECK(GetTempPathW(MAX_PATH,temp));CHECK(GetTempFileNameW(temp,L"tsw",0,dir));
    CHECK(DeleteFileW(dir));CHECK(CreateDirectoryW(dir,NULL));
    CHECK(swprintf(input,MAX_PATH,L"%ls\\entrada-\x00f1-\x65e5.bin",dir)>0);
    CHECK(swprintf(output,MAX_PATH,L"%ls\\salida-\x00f1-\x65e5.bin",dir)>0);
    CHECK(swprintf(invalid,MAX_PATH,L"%ls\\invalid.bin",dir)>0);
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",NULL);SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",NULL);
    CHECK(topspin_image_handoff_init());CHECK(initial_size==0 && callback);
    puts("PASS empty environment installs original callback without payload");
    CHECK(write_bytes(input,page,4096));
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",input);SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",output);
    CHECK(write_bytes(output,page,17));CHECK(topspin_image_handoff_init());
    CHECK(initial_size==4096&&!memcmp(initial,page,4096));CHECK(GetFileAttributesW(output)==INVALID_FILE_ATTRIBUTES);
    puts("PASS Unicode input exact bytes and stale output removal");
    unsigned before=shutdowns;callback(1,page,4096);callback(2,page,4095);callback(2,NULL,4096);
    CHECK(shutdowns==before+3);CHECK(GetFileAttributesW(output)==INVALID_FILE_ATTRIBUTES);
    puts("PASS invalid firmware routine/size/payload never publishes");
    callback(2,page,4096);CHECK(read_exact(output,got)&&!memcmp(got,page,4096));
    memset(page,0xA7,4096);callback(2,page,4096);CHECK(read_exact(output,got)&&!memcmp(got,page,4096));
    puts("PASS exact publication and replacing an existing output on Windows");
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",output);SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",output);
    CHECK(topspin_image_handoff_init());CHECK(!memcmp(initial,page,4096));
    CHECK(GetFileAttributesW(output)==INVALID_FILE_ATTRIBUTES);callback(2,page,4096);CHECK(read_exact(output,got));
    puts("PASS same IN/OUT reads payload before removing old record");
    for(unsigned bytes=4095;bytes<=4097;bytes+=2){
        CHECK(write_bytes(invalid,page,bytes));SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",invalid);
        CHECK(!topspin_image_handoff_init());CHECK(read_exact(output,got)&&!memcmp(got,page,4096));
    }
    puts("PASS 4095/4097-byte inputs fail without deleting old output");
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",dir);CHECK(!topspin_image_handoff_init());
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",NULL);SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",dir);
    CHECK(!topspin_image_handoff_init());CHECK(GetFileAttributesW(dir)&FILE_ATTRIBUTE_DIRECTORY);
    puts("PASS input/output directories rejected without deleting directory");
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",output);CHECK(topspin_image_handoff_init());
    CHECK(CreateDirectoryW(output,NULL));callback(2,page,4096);CHECK(GetFileAttributesW(output)&FILE_ATTRIBUTE_DIRECTORY);
    CHECK(RemoveDirectoryW(output));
    WIN32_FIND_DATAW find;wchar_t pattern[MAX_PATH];CHECK(swprintf(pattern,MAX_PATH,L"%ls\\*.tmp",dir)>0);
    HANDLE h=FindFirstFileW(pattern,&find);CHECK(h==INVALID_HANDLE_VALUE);
    puts("PASS failed atomic replacement preserves destination and cleans own temporary");
    DeleteFileW(input);DeleteFileW(invalid);CHECK(RemoveDirectoryW(dir));
    SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_IN",NULL);SetEnvironmentVariableW(L"TOPSPIN_LAUNCH_PAGE_OUT",NULL);
    CHECK(topspin_image_handoff_init());
    puts("8 native Windows handoff scenarios PASS; no game/save filesystem used");return 0;
}
