/* Actual Windows kernel_file + native filesystem transactions. djultra64.
 * Synthetic file bytes only; no title assets or controlled file-I/O mocks. */
#include "kernel/kernel.h"
#include "material_save_transaction.h"
#include "material_save_transaction_windows.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s (winerr=%lu)\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
void xbox_log(int level,const char *category,const char *format,...){(void)level;(void)category;(void)format;}
/* Xbox device translation is outside this fixture; the real kernel accepts
 * absolute UTF-8 fixture paths through this narrow path translation boundary. */
BOOL xbox_translate_path(const char *input,xbox_host_char *out,DWORD capacity){return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input,-1,out,(int)capacity)!=0;}
/* Execute the real SDK import bridge body, not a reimplementation. Only its
 * guest-stack token boundary is supplied here; native close/tracking is real. */
static uint32_t g_eax,close_token;
static HANDLE close_native;
#define KERNEL_LOG_ON() 0
#define STACK_ARG(n) close_token
static void bridge_forget_async_handle(uint32_t token){CHECK(token==close_token);}
static HANDLE bridge_take_handle(uint32_t token){CHECK(token==close_token);return close_native;}
#include "actual_ntclose_bridge.h"
static NTSTATUS bridge_close(HANDLE file){close_token=0x80000001u;close_native=file;g_eax=0xffffffffu;bridge_NtClose();return (NTSTATUS)g_eax;}
#undef KERNEL_LOG_ON
#undef STACK_ARG
static void utf8(const WCHAR *input,char output[4096]){CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input,-1,output,4096,NULL,NULL));}
static void append(WCHAR output[1024],const WCHAR *a,const WCHAR *b){CHECK(_snwprintf(output,1024,L"%ls\\%ls",a,b)>0);}
static void direct_write(const WCHAR *path,const char *data){HANDLE h=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(h!=INVALID_HANDLE_VALUE);DWORD wrote;CHECK(WriteFile(h,data,(DWORD)strlen(data),&wrote,NULL)&&wrote==strlen(data));CHECK(CloseHandle(h));}
static void direct_expect(const WCHAR *path,const char *data){HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(h!=INVALID_HANDLE_VALUE);char buffer[128];DWORD got;CHECK(ReadFile(h,buffer,sizeof buffer,&got,NULL)&&got==strlen(data)&&!memcmp(buffer,data,got));CloseHandle(h);}
static HANDLE guest_file(const WCHAR *path,ULONG disposition,ACCESS_MASK access,HANDLE parent){char text[4096];utf8(path,text);XBOX_ANSI_STRING name={(USHORT)strlen(text),(USHORT)(strlen(text)+1),text};XBOX_OBJECT_ATTRIBUTES oa={parent,&name,0};XBOX_IO_STATUS_BLOCK ios={0};HANDLE h=NULL;NTSTATUS status=xbox_NtCreateFile(&h,access,&oa,&ios,NULL,0,7,disposition,0);return status==STATUS_SUCCESS?h:NULL;}
static HANDLE guest_directory(const WCHAR *path){char text[4096];utf8(path,text);XBOX_ANSI_STRING name={(USHORT)strlen(text),(USHORT)(strlen(text)+1),text};XBOX_OBJECT_ATTRIBUTES oa={NULL,&name,0};XBOX_IO_STATUS_BLOCK ios={0};HANDLE h=NULL;CHECK(xbox_NtCreateFile(&h,XBOX_GENERIC_READ,&oa,&ios,NULL,0,7,XBOX_FILE_OPEN,XBOX_FILE_DIRECTORY_FILE)==STATUS_SUCCESS);return h;}
static NTSTATUS guest_write(HANDLE file,const char *data){XBOX_IO_STATUS_BLOCK ios={0};NTSTATUS result=xbox_NtWriteFile(file,NULL,NULL,NULL,&ios,(void*)data,(ULONG)strlen(data),NULL);if(result==STATUS_SUCCESS)CHECK(ios.Information==strlen(data));return result;}
static void guest_expect(const WCHAR *path,const char *data){HANDLE file=guest_file(path,XBOX_FILE_OPEN,XBOX_GENERIC_READ,NULL);CHECK(file);char buffer[128];XBOX_IO_STATUS_BLOCK ios={0};CHECK(xbox_NtReadFile(file,NULL,NULL,NULL,&ios,buffer,sizeof buffer,NULL)==STATUS_SUCCESS);CHECK(ios.Information==strlen(data)&&!memcmp(buffer,data,strlen(data)));CHECK(bridge_close(file)==STATUS_SUCCESS);}
static void make_dir(const WCHAR *p){CHECK(CreateDirectoryW(p,NULL)||GetLastError()==ERROR_ALREADY_EXISTS);}
static void setup(WCHAR root[1024],WCHAR title[1024],WCHAR original[1024],WCHAR runtime[1024],WCHAR scratch[1024]){
 WCHAR temp[1024],name[1024],path[1024];CHECK(GetTempPathW(1024,temp));CHECK(GetTempFileNameW(temp,L"msg",0,name));CHECK(DeleteFileW(name));CHECK(_snwprintf(root,1024,L"%ls-ñ-球",name)>0);make_dir(root);
 append(path,root,L"UserData");make_dir(path);append(title,path,L"4d530035");make_dir(title);append(original,title,L"player.dat");direct_write(original,"original");
 append(path,root,L"Cache");make_dir(path);append(scratch,path,L"TmpStorage");make_dir(scratch);WCHAR sentinel[1024];append(sentinel,scratch,L"sentinel.dat");direct_write(sentinel,"cache");
 WCHAR media[1024],creator[1024];append(media,path,L"Media");make_dir(media);append(creator,media,L"PlayerCreatorPart");make_dir(creator);append(runtime,creator,L"RunTimeParams");make_dir(runtime);append(path,runtime,L"PlayerCreator.ADN");direct_write(path,"runtime");
}
int main(int argc,char **argv){
 CHECK(argc==2);int mode=atoi(argv[1]);WCHAR root[1024],title[1024],original[1024],runtime[1024],scratch[1024],path[1024];char root_utf8[4096];
 if(mode==99||mode==98||mode==14){FILE *f=fopen(mode==99?"save-reload-root.txt":mode==98?"save-lock-root.txt":"save-crash-root.txt","rb");CHECK(f);CHECK(fgets(root_utf8,sizeof root_utf8,f));fclose(f);CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root_utf8,-1,root,1024));append(title,root,L"UserData\\4d530035");append(original,title,L"player.dat");if(mode==98){CHECK(!mc_save_transaction_configure(root_utf8));puts("PASS: concurrent process cannot own title transaction namespace");return 0;}
 CHECK(mc_save_transaction_configure(root_utf8));guest_expect(original,mode==14?"original":"new-save");direct_expect(original,"original");
 if(mode==14){WCHAR pattern[1024];append(pattern,root,L"UserData\\.4d530035.g-*");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);CHECK(find==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND);CHECK(mc_save_transaction_begin());HANDLE file=guest_file(original,XBOX_FILE_OVERWRITE,XBOX_GENERIC_WRITE,NULL);CHECK(file&&guest_write(file,"recovered-save")==STATUS_SUCCESS);CHECK(bridge_close(file)==STATUS_SUCCESS);CHECK(mc_save_transaction_end(1));guest_expect(original,"recovered-save");puts("PASS: crashed private stage cleaned and later native save commits");return 0;}
 puts("PASS: fresh native process resolves committed generation");return 0;}
 setup(root,title,original,runtime,scratch);utf8(root,root_utf8);
 if(mode==12){append(path,root,L"UserData\\.4d530035.current");direct_write(path,"INVALID\n");CHECK(!mc_save_transaction_configure(root_utf8));direct_expect(original,"original");puts("PASS: malformed native manifest rejected without conversion");return 0;}
 CHECK(mc_save_transaction_configure(root_utf8));guest_expect(original,"original");
 if(mode==16){FILE *marker=fopen("save-lock-root.txt","wb");CHECK(marker);CHECK(fwrite(root_utf8,1,strlen(root_utf8),marker)==strlen(root_utf8));CHECK(!fclose(marker));WCHAR executable[1024],command[2200];CHECK(GetModuleFileNameW(NULL,executable,1024));CHECK(_snwprintf(command,2200,L"\"%ls\" 98",executable)>0);STARTUPINFOW startup={sizeof startup};PROCESS_INFORMATION process;CHECK(CreateProcessW(executable,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process));CHECK(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);DWORD status;CHECK(GetExitCodeProcess(process.hProcess,&status)&&status==0);CloseHandle(process.hProcess);CloseHandle(process.hThread);}

 HANDLE old=mode==2?guest_file(original,XBOX_FILE_OPEN,XBOX_GENERIC_READ|XBOX_GENERIC_WRITE,NULL):NULL;if(mode==2)CHECK(old);
 HANDLE parent=mode==7?guest_directory(title):NULL;
 if(mode==8){append(path,title,L"alias.dat");CHECK(CreateHardLinkW(path,original,NULL));CHECK(!mc_save_transaction_begin());direct_expect(original,"original");puts("PASS: native hard-link source rejected");return 0;}
 CHECK(mc_save_transaction_begin());
 if(mode==2){CHECK(guest_write(old,"BAD")!=STATUS_SUCCESS);CHECK(mc_save_transaction_failed());CHECK(!mc_save_transaction_end(1));CHECK(bridge_close(old)==STATUS_SUCCESS);direct_expect(original,"original");puts("PASS: stale original handle cannot mutate during transaction");return 0;}
 if(mode==3){append(path,title,L"..\\outside.dat");CHECK(!guest_file(path,XBOX_FILE_OVERWRITE_IF,XBOX_GENERIC_WRITE,NULL));CHECK(!mc_save_transaction_end(1));direct_expect(original,"original");puts("PASS: native traversal rejected before mutation");return 0;}
 if(mode==10){append(path,root,L"outside.dat");CHECK(!guest_file(path,XBOX_FILE_OVERWRITE_IF,XBOX_GENERIC_WRITE,NULL));CHECK(!mc_save_transaction_end(1));CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);puts("PASS: transaction rejects outside mutation");return 0;}
 HANDLE file=guest_file(mode==7?L"player.dat":original,XBOX_FILE_OVERWRITE,XBOX_GENERIC_READ|XBOX_GENERIC_WRITE,parent);CHECK(file);CHECK(guest_write(file,"new-save")==STATUS_SUCCESS);
 direct_expect(original,"original");guest_expect(original,"new-save");
 if(mode==13){FILE *marker=fopen("save-crash-root.txt","wb");CHECK(marker);CHECK(fwrite(root_utf8,1,strlen(root_utf8),marker)==strlen(root_utf8));CHECK(!fclose(marker));puts("PASS: abrupt native exit leaves unpublished private stage");ExitProcess(0);}

 if(mode==4){CHECK(!mc_save_transaction_end(1));CHECK(bridge_close(file)==STATUS_SUCCESS);direct_expect(original,"original");puts("PASS: native open staged handle rejects publication");return 0;}
 if(mode==5){CHECK(bridge_close(file)==STATUS_SUCCESS);direct_write(original,"external");CHECK(!mc_save_transaction_end(1));direct_expect(original,"external");puts("PASS: concurrent original change rejects publication");return 0;}
 if(mode==6){LARGE_INTEGER size;size.QuadPart=128u*1024u*1024u+1;XBOX_IO_STATUS_BLOCK ios={0};CHECK(xbox_NtSetInformationFile(file,&ios,&size,sizeof size,XboxFileEndOfFileInformation)==STATUS_SUCCESS);CHECK(bridge_close(file)==STATUS_SUCCESS);CHECK(!mc_save_transaction_end(1));direct_expect(original,"original");puts("PASS: native size budget rejects oversized stage");return 0;}
 if(mode==9){XBOX_IO_STATUS_BLOCK ios={0};LARGE_INTEGER size={0};CHECK(xbox_NtSetInformationFile(file,&ios,&size,0,XboxFileEndOfFileInformation)!=STATUS_SUCCESS);CHECK(bridge_close(file)==STATUS_SUCCESS);CHECK(!mc_save_transaction_end(1));direct_expect(original,"original");puts("PASS: invalid native metadata request poisons save");return 0;}
 CHECK(bridge_close(file)==STATUS_SUCCESS);if(parent)CHECK(bridge_close(parent)==STATUS_SUCCESS);
 append(path,runtime,L"PlayerCreator.ADN");guest_expect(path,"runtime");HANDLE runtime_file=guest_file(path,XBOX_FILE_OVERWRITE,XBOX_GENERIC_READ|XBOX_GENERIC_WRITE,NULL);CHECK(runtime_file);CHECK(guest_write(runtime_file,"new-runtime")==STATUS_SUCCESS);CHECK(bridge_close(runtime_file)==STATUS_SUCCESS);guest_expect(path,"new-runtime");direct_expect(path,"runtime");
 WCHAR temp_file[1024];append(temp_file,scratch,L"new.tmp");HANDLE temporary_file=guest_file(temp_file,XBOX_FILE_OVERWRITE_IF,XBOX_GENERIC_WRITE,NULL);CHECK(temporary_file);CHECK(guest_write(temporary_file,"temporary")==STATUS_SUCCESS);CHECK(bridge_close(temporary_file)==STATUS_SUCCESS);CHECK(GetFileAttributesW(temp_file)==INVALID_FILE_ATTRIBUTES);
 CHECK(mc_save_transaction_end(mode!=1)==(mode!=1));
 if(mode==11||mode==15){
  CHECK(mc_save_transaction_begin());HANDLE second=guest_file(original,XBOX_FILE_OVERWRITE,XBOX_GENERIC_WRITE,NULL);CHECK(second&&guest_write(second,"second-save")==STATUS_SUCCESS);CHECK(bridge_close(second)==STATUS_SUCCESS);
  HANDLE locked=NULL;if(mode==15){append(path,root,L"UserData\\.4d530035.current");locked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(locked!=INVALID_HANDLE_VALUE);}
  CHECK(mc_save_transaction_end(1)==(mode!=15));guest_expect(original,mode==15?"new-save":"second-save");direct_expect(original,"original");
  if(locked){CloseHandle(locked);puts("PASS: native failed manifest replacement preserves last complete generation");}
  else{WCHAR pattern[1024];append(pattern,root,L"UserData\\.4d530035.g-*");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);CHECK(find!=INVALID_HANDLE_VALUE);unsigned count=1;while(FindNextFileW(find,&data))count++;FindClose(find);CHECK(count==1);puts("PASS: repeated native commit advances manifest and bounds old generation cleanup");}return 0;
 }
 guest_expect(original,mode==1?"original":"new-save");direct_expect(original,"original");guest_expect(path,"runtime");
 append(path,scratch,L"sentinel.dat");direct_expect(path,"cache");
 if(mode==0){FILE *marker=fopen("save-reload-root.txt","wb");CHECK(marker);CHECK(fwrite(root_utf8,1,strlen(root_utf8),marker)==strlen(root_utf8));CHECK(!fclose(marker));}
 puts("PASS: native whole-title publication or rollback, exact bytes, Unicode, temp/runtime isolation");return 0;
}
