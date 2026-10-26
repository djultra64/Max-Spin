/* Frozen authenticated athlete resources. Own work: djultra64.
 * The portable backend runs this same parser/router against synthetic CPU
 * fixtures. Shipping Windows builds use only Win32 handles and CryptoAPI. */
#define _POSIX_C_SOURCE 200809L
#include "character_windows.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
typedef HANDLE File;
#define BAD_FILE INVALID_HANDLE_VALUE
static SRWLOCK mutex=SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&mutex)
#define UNLOCK() ReleaseSRWLockExclusive(&mutex)
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <openssl/evp.h>
typedef int File;
#define BAD_FILE (-1)
static atomic_flag mutex=ATOMIC_FLAG_INIT;
#define LOCK() while(atomic_flag_test_and_set(&mutex)){}
#define UNLOCK() atomic_flag_clear(&mutex)
#endif
#define PATH_COUNT 32768u
#define MAX_RECORDS 96u
#define MAX_HELD 8192u
#define MAX_BYTES (128u*1024u*1024u)
#define MAX_MANIFEST (12u+MAX_RECORDS*(2u+240u+8u+64u))
struct Record {char path[241];};
static struct {
 int state;unsigned records,held_count;
 wchar_t assets[PATH_COUNT],session[PATH_COUNT];
 struct Record record[MAX_RECORDS];File held[MAX_HELD];
} current;
static int fold(int c){return c>='A'&&c<='Z'?c+('a'-'A'):c;}
static int equal(const char *a,const char *b){while(*a&&*b&&fold((unsigned char)*a)==fold((unsigned char)*b)){a++;b++;}return !*a&&!*b;}
static int prefix(const char *a,const char *b){while(*b){if(fold((unsigned char)*a++)!=fold((unsigned char)*b++))return 0;}return 1;}
static void close_file(File f){
#ifdef _WIN32
 CloseHandle(f);
#else
 close(f);
#endif
}
static int retain(File f){if(f==BAD_FILE)return 0;if(current.held_count==MAX_HELD){close_file(f);return 0;}current.held[current.held_count++]=f;return 1;}
static void reset(void){for(unsigned i=0;i<current.held_count;i++)close_file(current.held[i]);memset(&current,0,sizeof current);}
void topspin_windows_character_session_close(void){LOCK();reset();UNLOCK();}
static int canonical(const wchar_t *in,wchar_t *out){
 if(!in||!*in)return 0;
#ifdef _WIN32
 DWORD n=GetFullPathNameW(in,PATH_COUNT,out,NULL);if(!n||n>=PATH_COUNT)return 0;
 if(!((out[0]>=L'A'&&out[0]<=L'Z')||(out[0]>=L'a'&&out[0]<=L'z'))||out[1]!=L':'||(out[2]!=L'\\'&&out[2]!=L'/'))return 0;
#else
 size_t n=wcslen(in);if(in[0]!=L'/'||n>=PATH_COUNT)return 0;wcscpy(out,in);
#endif
 for(wchar_t *p=out;*p;p++)if(*p==L'\\')*p=L'/';
 size_t length=wcslen(out);while(length>3&&out[length-1]==L'/')out[--length]=0;
 return 1;
}
static int joined(wchar_t *out,const wchar_t *root,const char *relative){
 size_t a=wcslen(root),b=strlen(relative);if(a+b+2>PATH_COUNT)return 0;memcpy(out,root,a*sizeof *out);out[a++]=L'/';
 for(size_t i=0;i<=b;i++)out[a+i]=(unsigned char)relative[i];
 return 1;
}
/* Every ancestor remains open without write/delete sharing on Windows. This
 * prevents a junction/rename swap between verification and the SDK open. */
static File secure_file(const wchar_t *path,uint64_t limit,uint64_t *length){
#ifdef _WIN32
 wchar_t part[PATH_COUNT];size_t n=wcslen(path);if(n>=PATH_COUNT)return BAD_FILE;wcscpy(part,path);
 for(size_t i=3;i<=n;i++)if(!part[i]||part[i]==L'/'){
  wchar_t saved=part[i];part[i]=0;
  HANDLE f=CreateFileW(part,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
  part[i]=saved;if(f==INVALID_HANDLE_VALUE)return BAD_FILE;
  BY_HANDLE_FILE_INFORMATION info;
  int valid=GetFileType(f)==FILE_TYPE_DISK&&GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
  if(valid&&i<n)valid=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
  if(valid&&i==n){*length=((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow;valid=!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&*length<=limit;}
  if(!valid){CloseHandle(f);return BAD_FILE;}if(!retain(f))return BAD_FILE;if(i==n)return f;
 }
 return BAD_FILE;
#else
 char bytes[PATH_COUNT];if(wcstombs(bytes,path,sizeof bytes)>=sizeof bytes||bytes[0]!='/')return BAD_FILE;
 int parent=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(!retain(parent))return BAD_FILE;
 char *start=bytes+1;
 for(char *p=start;;p++)if(!*p||*p=='/'){
  int last=!*p;*p=0;if(!*start||!strcmp(start,".")||!strcmp(start,".."))return BAD_FILE;
  int f=openat(parent,start,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(last?0:O_DIRECTORY));if(f<0)return BAD_FILE;
  struct stat st;if(fstat(f,&st)||(last?(!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>limit):!S_ISDIR(st.st_mode))){close(f);return BAD_FILE;}
  if(!retain(f))return BAD_FILE;
  if(last){*length=(uint64_t)st.st_size;return f;}parent=f;start=p+1;
 }
#endif
}
static int read_bytes(File f,void *buffer,unsigned size,unsigned *got){
#ifdef _WIN32
 DWORD n;if(!ReadFile(f,buffer,size,&n,NULL))return 0;*got=n;return 1;
#else
 ssize_t n=read(f,buffer,size);if(n<0)return 0;*got=(unsigned)n;return 1;
#endif
}
static int hash_file(File f,unsigned char digest[32],unsigned char *capture,unsigned capacity,uint64_t length){
 unsigned char bytes[65536];unsigned got;uint64_t total=0;int ok=0;
#ifdef _WIN32
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;
 if(!CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))goto done;
#else
 EVP_MD_CTX *hash=EVP_MD_CTX_new();if(!hash||!EVP_DigestInit_ex(hash,EVP_sha256(),NULL))goto done;
#endif
 for(;;){if(!read_bytes(f,bytes,sizeof bytes,&got))goto done;if(!got)break;
  if(total+got>length||total+got>MAX_BYTES)goto done;
  if(capture){if(total+got>capacity)goto done;memcpy(capture+(size_t)total,bytes,got);}total+=got;
#ifdef _WIN32
  if(!CryptHashData(hash,bytes,got,0))goto done;
#else
  if(!EVP_DigestUpdate(hash,bytes,got))goto done;
#endif
 }
 if(total!=length)goto done;
#ifdef _WIN32
 {DWORD n=32;ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&n,0)&&n==32;}
#else
 {unsigned n=32;ok=EVP_DigestFinal_ex(hash,digest,&n)&&n==32;}
#endif
 done:
#ifdef _WIN32
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
#else
 EVP_MD_CTX_free(hash);
#endif
 return ok;
}
static void digest_text(const unsigned char bytes[32],char out[65]){static const char h[]="0123456789abcdef";for(unsigned i=0;i<32;i++){out[2*i]=h[bytes[i]>>4];out[2*i+1]=h[bytes[i]&15];}out[64]=0;}
static int supported(const char *path){
 static const char *names[]={"Sampras","HewittX","Gambill","GrosJea","Kuerten","ChangXX","BlakeXX","Robredo","TukurXX","Houston","WangXXX","KoenigX","GideXXX","Riosxxx","Emoryxx","NashXXX","HingisX","AnnaXXX","Barbara","Daniela","Dementi","Coetzer","Harkler","Shaughn","Yamanix","Baretto","Tellawi","Abercro","Auyeung","Hammond","sergeye","Hillxxx"};
 if(equal(path,"Media/Wads/Db.wad")||equal(path,"Media/Wads/GameIni.wad"))return 1;
 for(unsigned i=0;i<32;i++)for(unsigned outfit=0;outfit<2;outfit++){char expected[128];snprintf(expected,sizeof expected,"Media/Wads/Players/%s%02u.wad",names[i],outfit);if(equal(path,expected))return 1;}
 return 0;
}
static uint64_t little(const unsigned char *p,unsigned bytes){uint64_t result=0;for(unsigned i=0;i<bytes;i++)result|=(uint64_t)p[i]<<(8*i);return result;}
static int validate(const wchar_t *assets,const wchar_t *session,const char *pin,char digest[65]){
 unsigned char manifest[MAX_MANIFEST],sha[32];wchar_t path[PATH_COUNT];uint64_t size;reset();current.state=-1;
 if(digest)digest[0]=0;
 if(!canonical(assets,current.assets)||!canonical(session,current.session)||!joined(path,current.session,"manifest.bin"))goto fail;
 File file=secure_file(path,MAX_MANIFEST,&size);if(file==BAD_FILE||size<12||!hash_file(file,sha,manifest,sizeof manifest,size))goto fail;
 char actual[65];digest_text(sha,actual);if(pin&&(!equal(pin,actual)||strlen(pin)!=64))goto fail;
 const uint64_t manifest_size=size;
 if(memcmp(manifest,"MSCHAR1\0",8))goto fail;
 unsigned records=(unsigned)little(manifest+8,4);if(records>MAX_RECORDS)goto fail;
 size_t offset=12;
 for(unsigned i=0;i<records;i++){
  if(offset+2>manifest_size)goto fail;
  unsigned length=(unsigned)little(manifest+offset,2);offset+=2;
  if(!length||length>240||offset+length+72>manifest_size)goto fail;
  for(unsigned j=0;j<length;j++)if(manifest[offset+j]<32||manifest[offset+j]>126)goto fail;
  memcpy(current.record[i].path,manifest+offset,length);current.record[i].path[length]=0;offset+=length;
  if(!supported(current.record[i].path))goto fail;
  for(unsigned j=0;j<i;j++)if(equal(current.record[j].path,current.record[i].path))goto fail;
  uint64_t replacement_size=little(manifest+offset,8);offset+=8;if(replacement_size>MAX_BYTES)goto fail;
  if(!joined(path,current.assets,current.record[i].path))goto fail;
  file=secure_file(path,MAX_BYTES,&size);if(file==BAD_FILE||!hash_file(file,sha,NULL,0,size)||memcmp(sha,manifest+offset,32))goto fail;offset+=32;
  char leaf[32];snprintf(leaf,sizeof leaf,"files/%04u.wad",i);if(!joined(path,current.session,leaf))goto fail;
  file=secure_file(path,MAX_BYTES,&size);if(file==BAD_FILE||size!=replacement_size||!hash_file(file,sha,NULL,0,size)||memcmp(sha,manifest+offset,32))goto fail;offset+=32;

 }
 if(offset!=manifest_size)goto fail;
 current.records=records;current.state=1;if(digest)memcpy(digest,actual,65);return 1;
 fail:reset();current.state=-1;return 0;
}
int topspin_windows_character_session_validate(const wchar_t *assets,const wchar_t *session,char digest[65]){LOCK();int ok=validate(assets,session,NULL,digest);UNLOCK();return ok;}
static int env(const char *key,wchar_t *out){
#ifdef _WIN32
 wchar_t name[128];for(unsigned i=0;;i++){name[i]=(unsigned char)key[i];if(!key[i])break;}
 DWORD n=GetEnvironmentVariableW(name,out,PATH_COUNT);return !n?0:n>=PATH_COUNT?-1:1;
#else
 const char *v=getenv(key);if(!v||!*v)return 0;size_t n=mbstowcs(out,v,PATH_COUNT);return n==(size_t)-1||n>=PATH_COUNT?-1:1;
#endif
}
static void lazy(void){wchar_t assets[PATH_COUNT],session[PATH_COUNT],pin[PATH_COUNT];int a=env("MAX_SPIN_CHARACTER_ASSETS",assets),s=env("MAX_SPIN_CHARACTER_SESSION",session),p=env("MAX_SPIN_CHARACTER_MANIFEST_SHA256",pin);
 if(!a&&!s&&!p){current.state=2;return;}current.state=-1;if(a!=1||s!=1||p!=1||wcslen(pin)!=64)return;
 char digest[65];for(unsigned i=0;i<64;i++){if(pin[i]>127)return;digest[i]=(char)pin[i];}digest[64]=0;validate(assets,session,digest,NULL);
}
static int malformed(const char *path){
 const char *start=path;
 for(const char *p=path;;p++){
  if(*p==':'||(unsigned char)*p>126||(*p&&(unsigned char)*p<32))return 1;
  if(!*p||*p=='/'){
   size_t length=(size_t)(p-start);
   if(!length||(length==1&&start[0]=='.')||(length==2&&start[0]=='.'&&start[1]=='.'))return 1;
   if(!*p)return 0;
   start=p+1;
  }
 }
}
static int route(const char *guest,wchar_t *host,unsigned count){
 if(!guest||!host)return 0;
 const char *p=guest;
 if(prefix(p,"\\??\\"))p+=4;
 if(prefix(p,"D:")&&(p[2]=='/'||p[2]=='\\'))p+=3;
 else if(prefix(p,"\\Device\\CdRom0")&&(p[14]=='/'||p[14]=='\\'))p+=15;
 else return 0;
 char resource[241];unsigned n=0;while(*p&&n<240){unsigned c=(unsigned char)*p++;resource[n++]=c=='\\'?'/':(char)c;}resource[n]=0;
 if(prefix(resource,"Media/Wads/")&&(*p||malformed(resource))){if(!current.state)lazy();return current.state==2?0:-1;}
 if(*p)return 0;
 if(!supported(resource))return 0;
 if(!current.state)lazy();
 if(current.state==2)return 0;
 if(current.state!=1)return -1;
 for(unsigned i=0;i<current.records;i++)if(equal(resource,current.record[i].path)){
  wchar_t expected[PATH_COUNT],translated[PATH_COUNT],replacement[PATH_COUNT];char leaf[32];
  if(!joined(expected,current.assets,current.record[i].path)||!canonical(host,translated))return -1;
#ifdef _WIN32
  if(_wcsicmp(expected,translated))return -1;
#else
  if(wcscmp(expected,translated))return -1;
#endif
  snprintf(leaf,sizeof leaf,"files/%04u.wad",i);if(!joined(replacement,current.session,leaf)||wcslen(replacement)>=count)return -1;
  wcscpy(host,replacement);return 1;
 }
 return 0;
}
int topspin_windows_character_route(const char *guest,wchar_t *host,unsigned count){LOCK();int result=route(guest,host,count);UNLOCK();return result;}
