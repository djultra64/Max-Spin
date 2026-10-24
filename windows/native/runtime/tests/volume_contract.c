/* Native equivalent of the shared FATX volume regression, including the actual
 * volume-query import body. Synthetic image bytes only. Own work: djultra64. */
#include "kernel/kernel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s winerr=%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
static WCHAR image[MAX_PATH];
void xbox_log(int level,const char *category,const char *format,...){(void)level;(void)category;(void)format;}
BOOL xbox_translate_path(const char *input,xbox_host_char *out,DWORD capacity){(void)input;size_t length=wcslen(image);if(length>=capacity)return FALSE;memcpy(out,image,(length+1)*sizeof(WCHAR));return TRUE;}
static uint32_t g_eax,args[5],ios_status,ios_bytes;
static HANDLE native_file;
static unsigned char guest[256];
#define STACK_ARG(index) args[index]
#define XBOX_TO_NATIVE(value) ((value)?(void*)(guest+(value)):NULL)
#define KERNEL_LOG_ON() 0
static HANDLE bridge_resolve_handle(uint32_t token){CHECK(token==0x80000001u);return native_file;}
static HANDLE bridge_take_handle(uint32_t token){return bridge_resolve_handle(token);}
static void bridge_forget_async_handle(uint32_t token){CHECK(token==0x80000001u);}
static int bridge_buf_ok(uint32_t address,uint32_t length,const char *api){(void)api;return address>=64&&(uint64_t)address+length<=sizeof guest;}
static void bridge_write_iostatus(uint32_t address,NTSTATUS status,uint32_t bytes){CHECK(address==16);ios_status=(uint32_t)status;ios_bytes=bytes;}
#include "actual_volume_import.h"
#include "actual_ntclose_bridge.h"
static void close_guest(void){args[0]=0x80000001u;bridge_NtClose();CHECK(g_eax==STATUS_SUCCESS);}
static void make_image(unsigned sectors,const char *magic,uint64_t length){
 HANDLE file=CreateFileW(image,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
 unsigned char header[16]={0};memcpy(header,magic,4);header[8]=sectors&255;header[9]=(sectors>>8)&255;DWORD wrote;CHECK(WriteFile(file,header,sizeof header,&wrote,NULL)&&wrote==sizeof header);
 LARGE_INTEGER size;size.QuadPart=(LONGLONG)length;CHECK(SetFilePointerEx(file,size,NULL,FILE_BEGIN)&&SetEndOfFile(file));CHECK(CloseHandle(file));
}
static void query(unsigned sectors,unsigned options,int valid){
 make_image(sectors,"FATX",64u*1024u*1024u);
 XBOX_ANSI_STRING name={.Buffer="\\Device\\Harddisk0\\Partition5\\"};XBOX_OBJECT_ATTRIBUTES attributes={.ObjectName=&name};XBOX_IO_STATUS_BLOCK ios={0};
 CHECK(xbox_NtCreateFile(&native_file,0x100001,&attributes,&ios,NULL,0,3,XBOX_FILE_OPEN,options)==STATUS_SUCCESS);
 LARGE_INTEGER zero={0},position={0},after={0};
 int directory=(options&XBOX_FILE_DIRECTORY_FILE)!=0;
 if(!directory){position.QuadPart=(options&XBOX_FILE_NO_INTERMEDIATE_BUFFERING)?512:7;CHECK(SetFilePointerEx(native_file,position,NULL,FILE_BEGIN));}
 memset(guest,0xA5,sizeof guest);args[0]=0x80000001u;args[1]=16;args[2]=64;args[3]=24;args[4]=XboxFileFsSizeInformation;
 bridge_NtQueryVolumeInformationFile();CHECK(g_eax==STATUS_SUCCESS&&ios_status==STATUS_SUCCESS&&ios_bytes==24);
 XBOX_FILE_FS_SIZE_INFORMATION info;memcpy(&info,guest+64,sizeof info);
 CHECK(info.BytesPerSector==512&&info.SectorsPerAllocationUnit==(valid?sectors:32));
 if(valid){CHECK(info.TotalAllocationUnits.QuadPart==(64u*1024u*1024u)/(512u*sectors));CHECK(info.AvailableAllocationUnits.QuadPart<=info.TotalAllocationUnits.QuadPart);}
 if(!directory){CHECK(SetFilePointerEx(native_file,zero,&after,FILE_CURRENT));CHECK(after.QuadPart==position.QuadPart);}
 memset(guest,0xA5,sizeof guest);args[3]=8;bridge_NtQueryVolumeInformationFile();CHECK((int32_t)g_eax<0&&ios_status==g_eax&&ios_bytes==0);for(unsigned i=0;i<sizeof guest;i++)CHECK(guest[i]==0xA5);
 close_guest();
}
int main(void){
 WCHAR temporary[MAX_PATH],folder[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary)&&GetTempFileNameW(temporary,L"mvg",0,folder));CHECK(DeleteFileW(folder)&&CreateDirectoryW(folder,NULL));CHECK(_snwprintf(image,MAX_PATH,L"%ls\\Partition5.img",folder)>0);
 query(128,XBOX_FILE_DIRECTORY_FILE|0x20,1);
 query(128,XBOX_FILE_NO_INTERMEDIATE_BUFFERING,1);
 query(32,0x20,1);query(3,0x20,0);
 for(unsigned i=0;i<80;i++)query(128,XBOX_FILE_DIRECTORY_FILE|0x20,1);
 CHECK(DeleteFileW(image)&&RemoveDirectoryW(folder));
 puts("PASS: actual native volume import reports FATX128/32 geometry, redirected image identity, capacity, no-buffering offsets, short buffers, invalid header and close lifetime");return 0;
}
