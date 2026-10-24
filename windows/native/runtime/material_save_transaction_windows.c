/* Private whole-title generations with a single atomic native manifest commit.
 * Own work: djultra64. Original title file bytes and codec remain unchanged.
 * The original plain title directory is never converted, renamed or deleted. */
#define GUEST_TURN_NO_WRAPPERS
#include "guest_turn_windows.h"
#include "material_save_transaction_windows.h"
#include "material_save_transaction.h"
#include <aclapi.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define TX_PATH 1024u
#define TX_ENTRIES 4096u
#define TX_BYTES (128u*1024u*1024u)
#define TX_HANDLES 8192u
#define TX_PREFIX L".4d530035.g-"
static SRWLOCK transaction_lock=SRWLOCK_INIT;
static struct {
 int configured,active,failed;unsigned sequence;
 WCHAR root[TX_PATH],parent[TX_PATH],current[TX_PATH],stage[TX_PATH],work[TX_PATH],runtime_work[TX_PATH];
 WCHAR temporary[TX_PATH],runtime[TX_PATH],manifest[TX_PATH];
 HANDLE process_lock;
 uint64_t before;
 unsigned char old_manifest[128];DWORD old_manifest_bytes;
 PSECURITY_DESCRIPTOR security;
 BYTE user[256],owner[256];
 struct{HANDLE file;int scratch;} handles[TX_HANDLES];
} tx;
extern int xbox_SaveTransactionIOVersion(void);
static int join(WCHAR *out,const WCHAR *a,const WCHAR *b){size_t x=wcslen(a),y=wcslen(b);if(x+y+2>TX_PATH)return 0;memcpy(out,a,x*sizeof *out);out[x]=L'\\';memcpy(out+x+1,b,(y+1)*sizeof *out);return 1;}
static int child(const WCHAR *p,const WCHAR *root){size_t n=wcslen(root);return n&&!_wcsnicmp(p,root,n)&&(!p[n]||p[n]==L'\\');}
/* Collapse separators only. Reject dot traversal, ADS, trailing-space/dot
 * aliases, device/UNC namespaces and invalid UTF-16 paths before Win32 I/O. */
static int normalize(const WCHAR *path,WCHAR out[TX_PATH]){
 if(path&&!wcsncmp(path,L"\\\\?\\",4))path+=4;
 if(!path||!((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return 0;
 size_t at=3;out[0]=path[0];out[1]=L':';out[2]=L'\\';path+=3;
 while(*path){while(*path==L'\\'||*path==L'/')path++;if(!*path)break;
  const WCHAR *end=path;while(*end&&*end!=L'\\'&&*end!=L'/'){if(*end<L' '||*end==L':'||*end==L'*'||*end==L'?'||*end==L'"'||*end==L'<'||*end==L'>'||*end==L'|')return 0;end++;}
  size_t count=(size_t)(end-path);if(!count||(count==1&&path[0]==L'.')||(count==2&&path[0]==L'.'&&path[1]==L'.')||path[count-1]==L'.'||path[count-1]==L' '||at+count+2>TX_PATH)return 0;
  if(at>3)out[at++]=L'\\';memcpy(out+at,path,count*sizeof *out);at+=count;path=end;
 }
 out[at]=0;return 1;
}
static int final_path(HANDLE file,WCHAR out[TX_PATH]){
 WCHAR full[TX_PATH];DWORD n=GetFinalPathNameByHandleW(file,full,TX_PATH,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
 if(!n||n>=TX_PATH)return 0;
 const WCHAR *p=full;if(!wcsncmp(p,L"\\\\?\\",4))p+=4;
 return normalize(p,out);
}
static int owned(HANDLE file){PSID owner=NULL;PSECURITY_DESCRIPTOR security=NULL;
 DWORD code=GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,NULL,NULL,NULL,&security);
 int result=code==ERROR_SUCCESS&&owner&&(EqualSid(owner,tx.user)||EqualSid(owner,tx.owner));if(security)LocalFree(security);return result;
}
static HANDLE open_path(const WCHAR *path,DWORD access,DWORD share){return CreateFileW(path,access|READ_CONTROL,share,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);}
/* Check every existing ancestor without following a reparse point. Directory
 * handles pin each queried component while its attributes/identity are read. */
static int safe_path(const WCHAR *path,int allow_missing){
 WCHAR clean[TX_PATH],part[TX_PATH];if(!normalize(path,clean))return 0;
 size_t length=wcslen(clean);memcpy(part,clean,(length+1)*sizeof *part);
 for(size_t i=3;i<=length;i++)if(i==length||part[i]==L'\\'){
  WCHAR saved=part[i];part[i]=0;HANDLE h=open_path(part,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
  if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();part[i]=saved;return allow_missing&&(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND);}
  BY_HANDLE_FILE_INFORMATION info;int ok=GetFileInformationByHandle(h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
  if(i<length)ok=ok&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);CloseHandle(h);part[i]=saved;if(!ok)return 0;
 }
 return 1;
}
static int directory(const WCHAR *path,int create){if(create){SECURITY_ATTRIBUTES sa={sizeof sa,tx.security,FALSE};if(!CreateDirectoryW(path,&sa)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 0;}
 if(!safe_path(path,0))return 0;HANDLE h=open_path(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);if(h==INVALID_HANDLE_VALUE)return 0;
 BY_HANDLE_FILE_INFORMATION info;int ok=GetFileInformationByHandle(h,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&owned(h);CloseHandle(h);return ok;
}
static void hash(uint64_t *digest,const void *p,size_t bytes){const unsigned char *s=p;while(bytes--){*digest^=*s++;*digest*=UINT64_C(1099511628211);}}
static int same(const BY_HANDLE_FILE_INFORMATION *a,const BY_HANDLE_FILE_INFORMATION *b){return a->dwVolumeSerialNumber==b->dwVolumeSerialNumber&&a->nFileIndexHigh==b->nFileIndexHigh&&a->nFileIndexLow==b->nFileIndexLow&&a->dwFileAttributes==b->dwFileAttributes&&a->nNumberOfLinks==b->nNumberOfLinks&&a->nFileSizeHigh==b->nFileSizeHigh&&a->nFileSizeLow==b->nFileSizeLow&&!memcmp(&a->ftLastWriteTime,&b->ftLastWriteTime,sizeof(FILETIME))&&!memcmp(&a->ftCreationTime,&b->ftCreationTime,sizeof(FILETIME));}
static void identity_hash(uint64_t *digest,const BY_HANDLE_FILE_INFORMATION *info){hash(digest,&info->dwVolumeSerialNumber,sizeof info->dwVolumeSerialNumber);hash(digest,&info->nFileIndexHigh,sizeof info->nFileIndexHigh);hash(digest,&info->nFileIndexLow,sizeof info->nFileIndexLow);hash(digest,&info->dwFileAttributes,sizeof info->dwFileAttributes);hash(digest,&info->nFileSizeHigh,sizeof info->nFileSizeHigh);hash(digest,&info->nFileSizeLow,sizeof info->nFileSizeLow);hash(digest,&info->ftLastWriteTime,sizeof info->ftLastWriteTime);hash(digest,&info->ftCreationTime,sizeof info->ftCreationTime);}
static int name_compare(const void *a,const void *b){return wcscmp(*(WCHAR*const*)a,*(WCHAR*const*)b);}
/* Ordered, bounded, no-link tree copy or identity+content validation. The
 * exclusive stage-file handles at commit reject external untracked readers. */
static int tree(const WCHAR *source,const WCHAR *destination,int flush,unsigned depth,unsigned *entries,uint64_t *bytes,uint64_t *digest){
 if(depth>16||++*entries>TX_ENTRIES)return 0;
 HANDLE dir=open_path(source,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);if(dir==INVALID_HANDLE_VALUE)return 0;
 BY_HANDLE_FILE_INFORMATION before={0},after={0};int ok=GetFileInformationByHandle(dir,&before)&&(before.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(before.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&owned(dir);if(ok)identity_hash(digest,&before);
 WCHAR pattern[TX_PATH];WCHAR **names=calloc(TX_ENTRIES,sizeof *names);unsigned count=0;HANDLE find=INVALID_HANDLE_VALUE;WIN32_FIND_DATAW data;
 if(!names||!join(pattern,source,L"*"))ok=0;
 if(ok){find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)ok=0;}
 if(ok)for(;;){
  if(wcscmp(data.cFileName,L".")&&wcscmp(data.cFileName,L"..")){
   if(count==TX_ENTRIES){ok=0;break;}size_t n=wcslen(data.cFileName);names[count]=malloc((n+1)*sizeof(WCHAR));if(!names[count]){ok=0;break;}memcpy(names[count++],data.cFileName,(n+1)*sizeof(WCHAR));
  }
  if(!FindNextFileW(find,&data)){if(GetLastError()!=ERROR_NO_MORE_FILES)ok=0;break;}
 }
 if(find!=INVALID_HANDLE_VALUE)FindClose(find);if(ok)qsort(names,count,sizeof *names,name_compare);
 for(unsigned i=0;ok&&i<count;i++){
  if(++*entries>TX_ENTRIES){ok=0;break;}
  WCHAR in[TX_PATH],out[TX_PATH];if(!join(in,source,names[i])||(destination&&!join(out,destination,names[i]))){ok=0;break;}
  hash(digest,names[i],(wcslen(names[i])+1)*sizeof(WCHAR));
  HANDLE input=open_path(in,GENERIC_READ|(flush?GENERIC_WRITE:0),flush?0:(FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE));
  BY_HANDLE_FILE_INFORMATION a,b;
  if(input==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(input,&a)||(a.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!owned(input)){if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);ok=0;break;}
  if(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
   CloseHandle(input);if(destination&&!directory(out,1)){ok=0;break;}
   ok=tree(in,destination?out:NULL,flush,depth+1,entries,bytes,digest);
  }else{
   uint64_t size=((uint64_t)a.nFileSizeHigh<<32)|a.nFileSizeLow;
   if(a.nNumberOfLinks!=1||size>TX_BYTES-*bytes){CloseHandle(input);ok=0;break;}
   *bytes+=size;identity_hash(digest,&a);HANDLE output=INVALID_HANDLE_VALUE;
   if(destination){SECURITY_ATTRIBUTES sa={sizeof sa,tx.security,FALSE};output=CreateFileW(out,GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(output==INVALID_HANDLE_VALUE)ok=0;}
   unsigned char buffer[16384];uint64_t total=0;DWORD got;
   while(ok){if(!ReadFile(input,buffer,sizeof buffer,&got,NULL)){ok=0;break;}if(!got)break;total+=got;hash(digest,buffer,got);if(output!=INVALID_HANDLE_VALUE){DWORD wrote;if(!WriteFile(output,buffer,got,&wrote,NULL)||wrote!=got){ok=0;break;}}}
   if(total!=size||!GetFileInformationByHandle(input,&b)||!same(&a,&b))ok=0;
   if(flush&&!FlushFileBuffers(input))ok=0;
   if(output!=INVALID_HANDLE_VALUE){if(!FlushFileBuffers(output))ok=0;CloseHandle(output);}CloseHandle(input);
  }
 }
 for(unsigned i=0;i<count;i++)free(names[i]);free(names);
 if(!GetFileInformationByHandle(dir,&after)||!same(&before,&after))ok=0;CloseHandle(dir);return ok;
}
static int inspect(const WCHAR *source,const WCHAR *destination,int flush,uint64_t *digest,unsigned *entries,uint64_t *bytes){*digest=UINT64_C(14695981039346656037);return safe_path(source,0)&&tree(source,destination,flush,0,entries,bytes,digest);}
static int generation_name(const WCHAR *name){size_t prefix=wcslen(TX_PREFIX);if(wcslen(name)!=prefix+32||wcsncmp(name,TX_PREFIX,prefix))return 0;for(size_t i=prefix;i<prefix+32;i++)if(!((name[i]>=L'0'&&name[i]<=L'9')||(name[i]>=L'a'&&name[i]<=L'f')))return 0;return 1;}
static int fresh(WCHAR path[TX_PATH]){
 path[0]=0;
 for(unsigned attempt=0;attempt<32;attempt++){
  /* Random bytes from the OS, no process timestamp naming collisions. */
  unsigned char random[16];typedef BOOLEAN (WINAPI *Random)(PVOID,ULONG);
  HMODULE advapi=GetModuleHandleW(L"advapi32.dll");Random random_fn=advapi?(Random)(void*)GetProcAddress(advapi,"SystemFunction036"):NULL;
  if(!random_fn||!random_fn(random,sizeof random))return 0;
  WCHAR name[64];wcscpy(name,TX_PREFIX);size_t prefix=wcslen(name);static const WCHAR digits[]=L"0123456789abcdef";
  for(unsigned i=0;i<16;i++){name[prefix+2*i]=digits[random[i]>>4];name[prefix+2*i+1]=digits[random[i]&15];}name[prefix+32]=0;
  WCHAR candidate[TX_PATH];if(!join(candidate,tx.parent,name))return 0;SECURITY_ATTRIBUTES sa={sizeof sa,tx.security,FALSE};if(CreateDirectoryW(candidate,&sa)){wcscpy(path,candidate);return 1;}if(GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
 }return 0;
}
static int read_manifest(unsigned char output[128],DWORD *bytes){
 *bytes=0;HANDLE file=open_path(tx.manifest,GENERIC_READ,FILE_SHARE_READ);
 if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
 BY_HANDLE_FILE_INFORMATION info;DWORD got=0;int ok=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&info.nNumberOfLinks==1&&!info.nFileSizeHigh&&info.nFileSizeLow>0&&info.nFileSizeLow<128&&owned(file)&&ReadFile(file,output,128,&got,NULL)&&got==info.nFileSizeLow;
 CloseHandle(file);if(ok)*bytes=got;return ok;
}
static int scratch(const WCHAR *path){return child(path,tx.stage)||child(path,tx.work)||child(path,tx.runtime_work);}
static int remove_tree(const WCHAR *path,unsigned depth,unsigned *entries){
 if(depth>16||++*entries>TX_ENTRIES||!directory(path,0))return 0;
 WCHAR pattern[TX_PATH];if(!join(pattern,path,L"*"))return 0;WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)return 0;int ok=1;
 do{if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;WCHAR name[TX_PATH];if(++*entries>TX_ENTRIES||!join(name,path,data.cFileName)||(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){ok=0;break;}
  if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)ok=remove_tree(name,depth+1,entries);else{HANDLE file=open_path(name,DELETE|FILE_READ_ATTRIBUTES,0);if(file==INVALID_HANDLE_VALUE){ok=0;break;}BY_HANDLE_FILE_INFORMATION info;ok=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&info.nNumberOfLinks==1&&owned(file);if(ok){FILE_DISPOSITION_INFO del={TRUE};ok=SetFileInformationByHandle(file,FileDispositionInfo,&del,sizeof del);}CloseHandle(file);}if(!ok)break;
 }while(FindNextFileW(find,&data));FindClose(find);return ok&&RemoveDirectoryW(path);
}
static void cleanup_path(const WCHAR *path){if(path[0]&&_wcsicmp(path,tx.current)&&_wcsicmp(path,tx.root)){unsigned entries=0;(void)remove_tree(path,0,&entries);}}
static void cleanup_orphans(void){WCHAR pattern[TX_PATH];if(!join(pattern,tx.parent,L".4d530035.g-*"))return;WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)return;unsigned count=0;
 do{if(++count>16)break;WCHAR path[TX_PATH];if(!join(path,tx.parent,data.cFileName))continue;
  if(generation_name(data.cFileName))cleanup_path(path);
  else{size_t length=wcslen(data.cFileName),suffix=wcslen(L".current");
   if(length>suffix&&!wcscmp(data.cFileName+length-suffix,L".current")){
    data.cFileName[length-suffix]=0;
    if(generation_name(data.cFileName)&&!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){
     HANDLE file=open_path(path,DELETE|FILE_READ_ATTRIBUTES,0);if(file!=INVALID_HANDLE_VALUE){BY_HANDLE_FILE_INFORMATION info;
      if(GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&info.nNumberOfLinks==1&&owned(file)){FILE_DISPOSITION_INFO del={TRUE};(void)SetFileInformationByHandle(file,FileDispositionInfo,&del,sizeof del);}CloseHandle(file);
     }
    }
   }
  }
 }while(FindNextFileW(find,&data));FindClose(find);
}

int mc_save_transaction_configure(const char *root){
 int left=guest_turn_leave();AcquireSRWLockExclusive(&transaction_lock);int ok=0,step=1;
 if(tx.configured||!root||xbox_SaveTransactionIOVersion()!=3)goto done;
 step=2;HANDLE token;DWORD needed;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))goto done;
 BYTE user_data[512],owner_data[512];
 int got=GetTokenInformation(token,TokenUser,user_data,sizeof user_data,&needed)&&GetTokenInformation(token,TokenOwner,owner_data,sizeof owner_data,&needed);CloseHandle(token);if(!got)goto done;
 /* Elevated Windows tokens commonly create files owned by their default
  * TokenOwner (Administrators), even though TokenUser names the account.
  * Admit that exact effective owner, never arbitrary group ownership. */
 PSID default_owner=((TOKEN_OWNER*)owner_data)->Owner;
 if(GetLengthSid(default_owner)>sizeof tx.owner||!CopySid(sizeof tx.owner,tx.owner,default_owner))goto done;
 step=3;PSID sid=((TOKEN_USER*)user_data)->User.Sid;DWORD sid_bytes=GetLengthSid(sid);if(sid_bytes>sizeof tx.user||!CopySid(sizeof tx.user,tx.user,sid))goto done;
 step=4;WCHAR *sid_text=NULL;if(!ConvertSidToStringSidW(tx.user,&sid_text))goto done;
 WCHAR sddl[512];_snwprintf(sddl,512,L"D:P(A;OICI;FA;;;%ls)(A;OICI;FA;;;SY)",sid_text);LocalFree(sid_text);
 step=5;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&tx.security,NULL))goto done;
 step=6;WCHAR path[TX_PATH],clean[TX_PATH];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root,-1,path,TX_PATH)||!normalize(path,clean)||!directory(clean,0))goto done;
 step=7;if(!join(path,clean,L"UserData")||!directory(path,1))goto done;wcscpy(tx.parent,path);
 step=8;if(!join(tx.root,path,L"4d530035")||!directory(tx.root,1)||!join(tx.manifest,path,L".4d530035.current"))goto done;
 step=9;if(!join(tx.temporary,clean,L"Cache\\TmpStorage")||!join(tx.runtime,clean,L"Cache\\Media\\PlayerCreatorPart\\RunTimeParams"))goto done;
 step=10;if(!join(path,tx.parent,L".4d530035.lock"))goto done;
 step=11;SECURITY_ATTRIBUTES sa={sizeof sa,tx.security,FALSE};tx.process_lock=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,&sa,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
 if(tx.process_lock==INVALID_HANDLE_VALUE){tx.process_lock=NULL;goto done;}
 step=12;BY_HANDLE_FILE_INFORMATION lock_info;if(!GetFileInformationByHandle(tx.process_lock,&lock_info)||(lock_info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||lock_info.nNumberOfLinks!=1||!owned(tx.process_lock))goto done;
 step=13;if(!read_manifest(tx.old_manifest,&tx.old_manifest_bytes))goto done;
 step=14;wcscpy(tx.current,tx.root);
 if(tx.old_manifest_bytes){
  static const char magic[]="MAXSPIN1\n";size_t prefix=sizeof magic-1;
  if(tx.old_manifest_bytes<=prefix||memcmp(tx.old_manifest,magic,prefix)||tx.old_manifest[tx.old_manifest_bytes-1]!='\n')goto done;
  char name[128];size_t length=tx.old_manifest_bytes-prefix-1;if(length!=wcslen(TX_PREFIX)+32||length>=sizeof name||memchr(tx.old_manifest+prefix,0,length))goto done;memcpy(name,tx.old_manifest+prefix,length);name[length]=0;
  WCHAR wide[128];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,wide,128)||!generation_name(wide)||!join(tx.current,tx.parent,wide)||!directory(tx.current,0))goto done;
 }
 step=15;tx.configured=1;cleanup_orphans();ok=1;
 done:if(!ok){fprintf(stderr,"[CREATOR-MATERIAL] native Save configure rejected stage=%d Win32=%lu\n",step,GetLastError());if(tx.process_lock){CloseHandle(tx.process_lock);tx.process_lock=NULL;}if(tx.security){LocalFree(tx.security);tx.security=NULL;}}
 ReleaseSRWLockExclusive(&transaction_lock);guest_turn_return(left);return ok;
}
static int generations_within_budget(void){
 WCHAR pattern[TX_PATH];if(!join(pattern,tx.parent,L".4d530035.g-*"))return 0;
 WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
 unsigned count=0;do{if(++count>12){FindClose(find);return 0;}}while(FindNextFileW(find,&data));DWORD error=GetLastError();FindClose(find);return error==ERROR_NO_MORE_FILES;
}
int mc_save_transaction_begin(void){
 int left=guest_turn_leave();AcquireSRWLockExclusive(&transaction_lock);int ok=0;
 if(!tx.configured||tx.active)goto done;
 cleanup_orphans();if(!generations_within_budget()){tx.failed=1;goto done;}
 tx.stage[0]=tx.work[0]=tx.runtime_work[0]=0;tx.failed=0;
 unsigned entries=0;uint64_t bytes=0;
 if(!fresh(tx.stage)||!inspect(tx.current,tx.stage,0,&tx.before,&entries,&bytes)||!fresh(tx.work)||!fresh(tx.runtime_work))goto failed;
 if(GetFileAttributesW(tx.runtime)!=INVALID_FILE_ATTRIBUTES){uint64_t ignored;if(!inspect(tx.runtime,tx.runtime_work,0,&ignored,&entries,&bytes))goto failed;}
 else if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)goto failed;
 unsigned char observed_manifest[128];DWORD observed_bytes=0;
 if(!read_manifest(observed_manifest,&observed_bytes)||observed_bytes!=tx.old_manifest_bytes||memcmp(observed_manifest,tx.old_manifest,observed_bytes))goto failed;
 tx.active=1;ok=1;goto done;
 failed:tx.failed=1;cleanup_path(tx.stage);cleanup_path(tx.work);cleanup_path(tx.runtime_work);
 done:ReleaseSRWLockExclusive(&transaction_lock);guest_turn_return(left);return ok;
}
int xbox_save_path_transaction_windows(WCHAR *path,size_t capacity){
 AcquireSRWLockExclusive(&transaction_lock);int ok=1;
 if(tx.configured){WCHAR clean[TX_PATH],mapped[TX_PATH];if(!normalize(path,clean)){ok=0;goto done;}
  const WCHAR *base=NULL,*suffix=NULL;
  if(child(clean,tx.root)){base=tx.active?tx.stage:tx.current;suffix=clean+wcslen(tx.root);}
  else if(tx.active&&child(clean,tx.current)){base=tx.stage;suffix=clean+wcslen(tx.current);}
  else if(tx.active&&child(clean,tx.temporary)){base=tx.work;suffix=clean+wcslen(tx.temporary);}
  else if(tx.active&&child(clean,tx.runtime)){base=tx.runtime_work;suffix=clean+wcslen(tx.runtime);}
  size_t n=base?wcslen(base)+wcslen(suffix):wcslen(clean);if(n>=TX_PATH||n>=capacity){ok=0;goto done;}
  if(base){wcscpy(mapped,base);wcscat(mapped,suffix);}else wcscpy(mapped,clean);
  if(!safe_path(mapped,1)){ok=0;goto done;}memcpy(path,mapped,(n+1)*sizeof *path);
 }
 done:if(!ok&&tx.active)tx.failed=1;ReleaseSRWLockExclusive(&transaction_lock);return ok;
}
int xbox_save_mutation_transaction_windows(HANDLE file,const WCHAR *path){
 AcquireSRWLockExclusive(&transaction_lock);int ok=1;
 if(tx.active){WCHAR actual[TX_PATH],clean[TX_PATH];if(file&&file!=INVALID_HANDLE_VALUE){if(!final_path(file,actual)){ok=0;goto done;}path=actual;}
  if(!path||!normalize(path,clean)||!scratch(clean)||!safe_path(clean,1))ok=0;
 }
 done:if(!ok)tx.failed=1;ReleaseSRWLockExclusive(&transaction_lock);return ok;
}
int xbox_save_opened_transaction_windows(HANDLE file,const WCHAR *path){
 AcquireSRWLockExclusive(&transaction_lock);int ok=1;
 if(tx.configured){unsigned i;for(i=0;i<TX_HANDLES;i++)if(!tx.handles[i].file)break;
  if(i==TX_HANDLES){ok=0;if(tx.active)tx.failed=1;}else{tx.handles[i].file=file;tx.handles[i].scratch=tx.active&&scratch(path);}
 }
 ReleaseSRWLockExclusive(&transaction_lock);return ok;
}
void xbox_save_closed_transaction_windows(HANDLE file){AcquireSRWLockExclusive(&transaction_lock);for(unsigned i=0;i<TX_HANDLES;i++)if(tx.handles[i].file==file){tx.handles[i].file=NULL;break;}ReleaseSRWLockExclusive(&transaction_lock);}
void xbox_save_failed_transaction_windows(void){AcquireSRWLockExclusive(&transaction_lock);if(tx.active)tx.failed=1;ReleaseSRWLockExclusive(&transaction_lock);}
int mc_save_transaction_failed(void){AcquireSRWLockShared(&transaction_lock);int failed=tx.failed;ReleaseSRWLockShared(&transaction_lock);return failed;}
static int publish_manifest(void){
 const WCHAR *name=wcsrchr(tx.stage,L'\\');if(!name||!generation_name(++name))return 0;
 char utf8[128];if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,utf8,sizeof utf8,NULL,NULL))return 0;
 unsigned char data[128];int length=snprintf((char*)data,sizeof data,"MAXSPIN1\n%s\n",utf8);if(length<0||length>=(int)sizeof data)return 0;
 WCHAR temp[TX_PATH];int n=_snwprintf(temp,TX_PATH,L"%ls.current",tx.stage);if(n<0||n>=TX_PATH)return 0;
 SECURITY_ATTRIBUTES sa={sizeof sa,tx.security,FALSE};HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,NULL);if(file==INVALID_HANDLE_VALUE)return 0;
 DWORD wrote;int ok=WriteFile(file,data,(DWORD)length,&wrote,NULL)&&wrote==(DWORD)length&&FlushFileBuffers(file);CloseHandle(file);
 if(ok)ok=MoveFileExW(temp,tx.manifest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 if(!ok)DeleteFileW(temp);else{memcpy(tx.old_manifest,data,(size_t)length);tx.old_manifest_bytes=(DWORD)length;}return ok;
}
int mc_save_transaction_end(int success){
 int left=guest_turn_leave();AcquireSRWLockExclusive(&transaction_lock);int ok=0;
 if(!tx.active)goto done;
 WCHAR previous[TX_PATH];wcscpy(previous,tx.current);
 unsigned char manifest[128];DWORD manifest_bytes=0;unsigned entries=0;uint64_t bytes=0,now,stage_digest,runtime_digest;
 if(!success||tx.failed)goto finish;
 for(unsigned i=0;i<TX_HANDLES;i++)if(tx.handles[i].file&&tx.handles[i].scratch)goto finish;
 if(!read_manifest(manifest,&manifest_bytes)||manifest_bytes!=tx.old_manifest_bytes||memcmp(manifest,tx.old_manifest,manifest_bytes)||!inspect(tx.current,NULL,0,&now,&entries,&bytes)||now!=tx.before)goto finish;
 entries=0;bytes=0;if(!inspect(tx.stage,NULL,1,&stage_digest,&entries,&bytes)||!inspect(tx.runtime_work,NULL,1,&runtime_digest,&entries,&bytes)||!publish_manifest())goto finish;
 /* The manifest publication is the one commit point. No cleanup failure can
  * turn an already published title into a reported rollback. */
 wcscpy(tx.current,tx.stage);ok=1;cleanup_path(previous);
 finish:tx.active=0;tx.failed=!ok;cleanup_path(tx.work);cleanup_path(tx.runtime_work);if(!ok)cleanup_path(tx.stage);
 fprintf(stderr,"[CREATOR-MATERIAL] native Save transaction %s; complete title publication=%d\n",ok?"committed":"rejected",ok);
 done:ReleaseSRWLockExclusive(&transaction_lock);guest_turn_return(left);return ok;
}
/* Existing frontend hook signatures retained; Windows SDK uses the native
 * wide-path and HANDLE hooks above, never truncating a HANDLE to POSIX fd. */
int xbox_save_path_transaction(char *path,size_t capacity){WCHAR wide[TX_PATH];if(!path||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,TX_PATH)||!xbox_save_path_transaction_windows(wide,TX_PATH))return 0;int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,NULL,0,NULL,NULL);return bytes>0&&(size_t)bytes<=capacity&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,path,(int)capacity,NULL,NULL)!=0;}
int xbox_save_mutation_transaction(int fd,const char *path){(void)fd;WCHAR wide[TX_PATH];if(!path||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,TX_PATH)){xbox_save_failed_transaction_windows();return 0;}return xbox_save_mutation_transaction_windows(NULL,wide);}
