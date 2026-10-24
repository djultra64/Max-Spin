/* Native Windows exception/window/path support. Own work: djultra64. */
#define MAX_SPIN_HOST_WINDOWS_IMPLEMENTATION
#define VK_USE_PLATFORM_WIN32_KHR
#include "host_windows.h"
#include <windows.h>
#include <vulkan/vulkan.h>
#include "d3d8_vk.h"
#include "d3d8_xbox.h"
#include <string.h>
#include <wchar.h>
#include <io.h>
#include <errno.h>
#include <stdarg.h>
#include <aclapi.h>
extern void guest_turn_set_code(const void*,size_t);
static HWND drawable;
static wchar_t *wide(const char *s) {
 if(!s)return NULL;int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);
 if(!n)return NULL;wchar_t *w=malloc((size_t)n*sizeof*w);if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,n)){free(w);w=NULL;}return w;
}
FILE *topspin_windows_fopen(const char *p,const char *m) {
 wchar_t *path=wide(p),*mode=wide(m);FILE *f=path&&mode?_wfopen(path,mode):NULL;free(path);free(mode);return f;
}
int topspin_windows_setenv(const char*n,const char*v,int overwrite) {if(!overwrite&&getenv(n))return 0;return _putenv_s(n,v);}
void topspin_windows_backtrace(void) {void *frames[24];USHORT count=CaptureStackBackTrace(0,24,frames,NULL);for(USHORT i=0;i<count;i++)fprintf(stderr,"[STACK] %p\n",frames[i]);}
int topspin_windows_register_guest_code(void) {
 const unsigned char *base=(const unsigned char*)GetModuleHandleW(NULL);
 const IMAGE_DOS_HEADER *dos=(const IMAGE_DOS_HEADER*)base;
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
 const IMAGE_NT_HEADERS64 *nt=(const IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE)return 0;
 const IMAGE_SECTION_HEADER *section=IMAGE_FIRST_SECTION(nt);
 uintptr_t first=UINTPTR_MAX,last=0;
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)if(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){
  uintptr_t lo=(uintptr_t)base+section[i].VirtualAddress,hi=lo+section[i].Misc.VirtualSize;
  if(lo<first)first=lo;if(hi>last)last=hi;
 }
 if(last>first){guest_turn_set_code((const void*)first,last-first);return 1;}
 fputs("[HOST] Missing executable PE sections\n",stderr);return 0;
}
uintptr_t topspin_windows_parent_window(void) {const char *s=getenv("MAX_SPIN_PARENT_HWND");if(!s)return 0;char *end;unsigned long long value=strtoull(s,&end,0);if(!*s||*end||!value||!IsWindow((HWND)(uintptr_t)value)){fputs("[HOST] Invalid parent HWND\n",stderr);exit(2);}return (uintptr_t)value;}
int topspin_windows_attach_drawable(uintptr_t hwnd,int*w,int*h) {RECT r;drawable=(HWND)hwnd;if(!GetClientRect(drawable,&r)||r.right<=0||r.bottom<=0)return 0;*w=r.right;*h=r.bottom;return 1;}
static int create_surface(void *instance,void *surface,void *unused) {
 (void)unused;VkWin32SurfaceCreateInfoKHR info={.sType=VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,.hinstance=GetModuleHandleW(NULL),.hwnd=drawable};
 PFN_vkCreateWin32SurfaceKHR create=(PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr((VkInstance)instance,"vkCreateWin32SurfaceKHR");
 return create&&create((VkInstance)instance,&info,NULL,(VkSurfaceKHR*)surface)==VK_SUCCESS;
}
static void size(int*w,int*h,void*unused) {(void)unused;RECT r={0};GetClientRect(drawable,&r);*w=r.right>0?r.right:1;*h=r.bottom>0?r.bottom:1;}
void topspin_windows_vulkan_host(D3D8VkHost*host,uintptr_t hwnd) {static const char*extensions[]={"VK_KHR_surface","VK_KHR_win32_surface"};drawable=(HWND)hwnd;memset(host,0,sizeof*host);host->instance_extensions=extensions;host->instance_extension_count=2;host->create_surface=create_surface;host->drawable_size=size;}

int topspin_windows_open(const char*p,int flags,...) {
 wchar_t*w=wide(p);if(!w){errno=EINVAL;return -1;}
 DWORD access=(flags&_O_RDWR)?GENERIC_READ|GENERIC_WRITE:(flags&_O_WRONLY)?GENERIC_WRITE:GENERIC_READ;
 DWORD disposition=(flags&_O_CREAT)?((flags&_O_EXCL)?CREATE_NEW:(flags&_O_TRUNC)?CREATE_ALWAYS:OPEN_ALWAYS):(flags&_O_TRUNC)?TRUNCATE_EXISTING:OPEN_EXISTING;
 HANDLE h=CreateFileW(w,access,FILE_SHARE_READ,NULL,disposition,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|((flags&O_DIRECTORY)?FILE_FLAG_BACKUP_SEMANTICS:0),NULL);free(w);
 if(h==INVALID_HANDLE_VALUE){errno=GetLastError()==ERROR_FILE_EXISTS?EEXIST:ENOENT;return -1;}
 BY_HANDLE_FILE_INFORMATION info;if(!GetFileInformationByHandle(h,&info)||((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(flags&O_DIRECTORY)))){CloseHandle(h);errno=EACCES;return -1;}
 int fd=_open_osfhandle((intptr_t)h,(flags&(_O_RDWR|_O_WRONLY|_O_APPEND))|_O_BINARY|_O_NOINHERIT);if(fd<0)CloseHandle(h);return fd;
}

char *topspin_windows_realpath(const char*p,char*out){wchar_t*w=wide(p);if(!w)return NULL;DWORD n=GetFullPathNameW(w,0,NULL,NULL);wchar_t*full=n?calloc(n,sizeof*full):NULL;if(!full||!GetFullPathNameW(w,n,full,NULL)){free(w);free(full);return NULL;}free(w);int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,full,-1,NULL,0,NULL,NULL);if(bytes<=0||bytes>4096){free(full);return NULL;}if(!out)out=malloc((size_t)bytes);if(out)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,full,-1,out,bytes,NULL,NULL);free(full);if(out)for(char*c=out;*c;c++)if(*c=='\\')*c='/';return out;}

char **topspin_windows_utf8_argv(int count,wchar_t**wide_args){char**args=calloc((size_t)count+1,sizeof*args);if(!args)return NULL;for(int i=0;i<count;i++){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_args[i],-1,NULL,0,NULL,NULL);if(n<=0||(args[i]=malloc((size_t)n))==NULL||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_args[i],-1,args[i],n,NULL,NULL))return NULL;}for(int i=1;i<count;i++)for(char*c=args[i];*c;c++)if(*c=='\\')*c='/';return args;}
int topspin_windows_poll_input(void){
 extern void xbox_HostInputFocus(int),xbox_HostInputKey(int,int);
 static HANDLE quit;static int initialized;static unsigned char before[256];
 if(!initialized){wchar_t name[128];if(GetEnvironmentVariableW(L"MAX_SPIN_QUIT_EVENT",name,128))quit=OpenEventW(SYNCHRONIZE,FALSE,name);initialized=1;}
 if(quit&&WaitForSingleObject(quit,0)==WAIT_OBJECT_0)return 1;
 if(!drawable||!IsWindow(drawable))return 1;
 int focus=GetForegroundWindow()==drawable;xbox_HostInputFocus(focus);
 for(int key=8;key<256;key++){unsigned char down=focus&&((GetAsyncKeyState(key)&0x8000)!=0);if(down!=before[key]){xbox_HostInputKey(key,down);before[key]=down;}}
 return 0;
}

int topspin_windows_fchmod(int fd,int mode){
 if(mode!=0600){errno=ENOSYS;return -1;}HANDLE token=NULL;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return -1;
 DWORD n=0;GetTokenInformation(token,TokenUser,NULL,0,&n);TOKEN_USER*user=malloc(n);int result=-1;
 if(user&&GetTokenInformation(token,TokenUser,user,n,&n)){
  EXPLICIT_ACCESSW access={0};access.grfAccessPermissions=FILE_ALL_ACCESS;access.grfAccessMode=SET_ACCESS;access.grfInheritance=NO_INHERITANCE;access.Trustee.TrusteeForm=TRUSTEE_IS_SID;access.Trustee.TrusteeType=TRUSTEE_IS_USER;access.Trustee.ptstrName=(LPWSTR)user->User.Sid;PACL acl=NULL;
  if(SetEntriesInAclW(1,&access,NULL,&acl)==ERROR_SUCCESS){if(SetSecurityInfo((HANDLE)_get_osfhandle(fd),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,acl,NULL)==ERROR_SUCCESS)result=0;LocalFree(acl);}
 }free(user);CloseHandle(token);if(result)errno=EACCES;return result;
}

static int user_owns_handle(HANDLE h){
 HANDLE token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return 0;unsigned char user[512],owner[512];DWORD n;int ok=GetTokenInformation(token,TokenUser,user,sizeof user,&n)&&GetTokenInformation(token,TokenOwner,owner,sizeof owner,&n);CloseHandle(token);if(!ok)return 0;
 PSID sid=NULL;PSECURITY_DESCRIPTOR descriptor=NULL;if(GetSecurityInfo(h,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&sid,NULL,NULL,NULL,&descriptor)!=ERROR_SUCCESS)return 0;ok=EqualSid(sid,((TOKEN_USER*)user)->User.Sid)||EqualSid(sid,((TOKEN_OWNER*)owner)->Owner);LocalFree(descriptor);return ok;
}
static int file_info(HANDLE h,struct topspin_windows_stat*s){
 BY_HANDLE_FILE_INFORMATION info;if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){errno=EACCES;return -1;}
 memset(s,0,sizeof*s);s->st_dev=info.dwVolumeSerialNumber;s->st_ino=((uint64_t)info.nFileIndexHigh<<32)|info.nFileIndexLow;s->st_size=((int64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow;s->st_mode=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?_S_IFDIR|0700:_S_IFREG|0600;s->st_uid=user_owns_handle(h)?0:1;
 FILE_BASIC_INFO basic;if(!GetFileInformationByHandleEx(h,FileBasicInfo,&basic,sizeof basic)){errno=EIO;return -1;}
 uint64_t m=(uint64_t)basic.LastWriteTime.QuadPart,c=(uint64_t)basic.ChangeTime.QuadPart;
 s->st_mtim.tv_sec=(time_t)(m/10000000);s->st_mtim.tv_nsec=(long)(m%10000000)*100;s->st_ctim.tv_sec=(time_t)(c/10000000);s->st_ctim.tv_nsec=(long)(c%10000000)*100;return 0;
}
int topspin_windows_fstat(int fd,struct topspin_windows_stat*s){return file_info((HANDLE)_get_osfhandle(fd),s);}
int topspin_windows_stat(const char*p,struct topspin_windows_stat*s){wchar_t*w=wide(p);if(!w){errno=EINVAL;return -1;}HANDLE h=CreateFileW(w,FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);free(w);if(h==INVALID_HANDLE_VALUE){errno=ENOENT;return -1;}int result=file_info(h,s);CloseHandle(h);return result;}
int topspin_windows_lstat(const char*p,struct topspin_windows_stat*s){return topspin_windows_stat(p,s);}
int topspin_windows_openat(int dir,const char*leaf,int flags,...){
 if(!leaf||!leaf[0]||strpbrk(leaf,"/\\:")||!strcmp(leaf,".")||!strcmp(leaf,"..")){errno=EINVAL;return -1;}
 wchar_t base[32768];DWORD n=GetFinalPathNameByHandleW((HANDLE)_get_osfhandle(dir),base,32768,FILE_NAME_NORMALIZED);if(!n||n>=32768)return -1;
 wchar_t*name=wide(leaf);if(!name||n+wcslen(name)+2>=32768){free(name);return -1;}wcscat(base,L"\\");wcscat(base,name);free(name);
 char*utf8;int bytes=WideCharToMultiByte(CP_UTF8,0,base,-1,NULL,0,NULL,NULL);utf8=malloc((size_t)bytes);if(!utf8)return -1;WideCharToMultiByte(CP_UTF8,0,base,-1,utf8,bytes,NULL,NULL);int result=topspin_windows_open(utf8,flags);free(utf8);return result;
}

/* Legacy Media Foundation upload API uses the selected backend device vtable. */
HRESULT d3d8_CreateTextureImpl(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,IDirect3DTexture8**out){IDirect3DDevice8*device=xbox_GetD3DDevice();if(!device||!out)return E_INVALIDARG;return device->lpVtbl->CreateTexture(device,w,h,levels,usage,format,D3DPOOL_DEFAULT,out);}
