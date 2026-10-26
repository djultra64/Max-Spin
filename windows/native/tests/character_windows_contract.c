/* Synthetic native session fixtures. Own work: djultra64. No game data. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef TOPSPIN_CHARACTER_WRAPPER_TEST
#include <windows.h>
#endif
#include "character_windows.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static unsigned char manifest[32768];static size_t used;
static wchar_t assets[1024],session[1024];static char root[1024];
#if defined(_WIN32) || defined(TOPSPIN_CHARACTER_WRAPPER_TEST)
static wchar_t translated[2048];
extern BOOL __wrap_xbox_translate_path(const char*,WCHAR*,DWORD);
BOOL __real_xbox_translate_path(const char *guest,WCHAR *host,DWORD count) {
 (void)guest;if(wcslen(translated)>=count)return FALSE;wcscpy(host,translated);return TRUE;
}
#endif
static const char original[]="original synthetic WAD\n",replacement[]="replacement synthetic WAD\n";
#ifdef TOPSPIN_CHARACTER_SDK_FILE_TEST
extern int character_sdk_read(const char*,const char*,unsigned);
#endif
static void directory(const char *p) {
#ifdef _WIN32
 _mkdir(p);
#else
 mkdir(p,0700);
#endif
}
static int write_bytes(const char *leaf,const void *bytes,size_t length) {
 char p[2048];snprintf(p,sizeof p,"%s/%s",root,leaf);FILE *f=fopen(p,"wb");if(!f)return 0;int ok=fwrite(bytes,1,length,f)==length;return fclose(f)==0&&ok;
}
static void hex(unsigned char *out,const char *in) {for(unsigned i=0;i<32;i++){unsigned v;sscanf(in+2*i,"%2x",&v);out[i]=(unsigned char)v;}}
static void number(uint64_t n,unsigned bytes){while(bytes--){manifest[used++]=(unsigned char)n;n>>=8;}}
static void record(const char *path) {
 size_t length=strlen(path);number(length,2);memcpy(manifest+used,path,length);used+=length;number(sizeof replacement-1,8);
 hex(manifest+used,"5800031901333a30b66ea73241eb070f2520bbfd43de84d62930f294c7be97ee");used+=32;
 hex(manifest+used,"335018c44927a13038da45a00b38fb584c7cff67144a178abb486094d657e031");used+=32;
}
static void fresh(const char *path) {memcpy(manifest,"MSCHAR1\0",8);used=8;number(1,4);record(path);}
static int save(void){return write_bytes("session/manifest.bin",manifest,used);}
static void environment(const char *name,const wchar_t *value) {
#ifdef _WIN32
 wchar_t key[128];mbstowcs(key,name,128);SetEnvironmentVariableW(key,value);
#else
 char text[4096];if(value){wcstombs(text,value,sizeof text);setenv(name,text,1);}else unsetenv(name);
#endif
}
static void pinned(const char *digest) {wchar_t value[65];mbstowcs(value,digest,65);environment("MAX_SPIN_CHARACTER_SESSION",session);environment("MAX_SPIN_CHARACTER_ASSETS",assets);environment("MAX_SPIN_CHARACTER_MANIFEST_SHA256",value);}
static void host_path(wchar_t *host,const char *resource){swprintf(host,2048,L"%ls/%hs",assets,resource);}
#ifdef _WIN32
/* Mount-point junctions require no symbolic-link privilege or administrator.
 * Both target and link are owned temporary fixture directories. */
static int junction(const wchar_t *link,const wchar_t *target){
 struct {DWORD tag;WORD data_length,reserved,sub_offset,sub_length,print_offset,print_length;WCHAR path[4096];} data={0};
 wchar_t native[2048];wcscpy(native,target);for(wchar_t *p=native;*p;p++)if(*p==L'/')*p=L'\\';
 data.tag=IO_REPARSE_TAG_MOUNT_POINT;data.sub_length=(WORD)((wcslen(native)+4)*sizeof(wchar_t));
 data.print_offset=data.sub_length+sizeof(wchar_t);data.print_length=(WORD)(wcslen(native)*sizeof(wchar_t));
 swprintf(data.path,4096,L"\\??\\%ls",native);wcscpy(data.path+data.print_offset/sizeof(wchar_t),native);
 data.data_length=(WORD)(8+data.print_offset+data.print_length+sizeof(wchar_t));
 if(!CreateDirectoryW(link,NULL))return 0;
 HANDLE f=CreateFileW(link,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);if(f==INVALID_HANDLE_VALUE)return 0;
 DWORD got;int ok=DeviceIoControl(f,FSCTL_SET_REPARSE_POINT,&data,data.data_length+8,NULL,0,&got,NULL)!=0;CloseHandle(f);return ok;
}
#endif
int main(void) {
 char p[2048],digest[65];wchar_t host[2048],expected[2048];
#ifdef _WIN32
 char temporary[MAX_PATH];CHECK(GetTempPathA(MAX_PATH,temporary));snprintf(root,sizeof root,"%sMaxSpin-character-%lu",temporary,(unsigned long)GetCurrentProcessId());
 for(char *slash=root;*slash;slash++)if(*slash=='\\')*slash='/';
#else
 snprintf(root,sizeof root,"/tmp/MaxSpin-character-XXXXXX");CHECK(mkdtemp(root));
#endif
 directory(root);snprintf(p,sizeof p,"%s/assets",root);directory(p);mbstowcs(assets,p,1024);
 snprintf(p,sizeof p,"%s/assets/Media",root);directory(p);snprintf(p,sizeof p,"%s/assets/Media/Wads",root);directory(p);
 snprintf(p,sizeof p,"%s/assets/Media/Wads/Players",root);directory(p);
 snprintf(p,sizeof p,"%s/session",root);directory(p);mbstowcs(session,p,1024);snprintf(p,sizeof p,"%s/session/files",root);directory(p);
 CHECK(write_bytes("assets/Media/Wads/Db.wad",original,sizeof original-1));CHECK(write_bytes("session/files/0000.wad",replacement,sizeof replacement-1));fresh("Media/Wads/Db.wad");CHECK(save());
 environment("MAX_SPIN_CHARACTER_SESSION",NULL);environment("MAX_SPIN_CHARACTER_ASSETS",NULL);environment("MAX_SPIN_CHARACTER_MANIFEST_SHA256",NULL);
 host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route("D:\\Media\\Wads\\Db.wad",host,2048)==0);
 CHECK(topspin_windows_character_session_validate(assets,session,digest));CHECK(!strcmp(digest,"e7d4938b7e820dc8d03460d8910ddac8bda68bc1b6f9551a0c0ce5bf7462d4d9"));
 swprintf(expected,2048,L"%ls/files/0000.wad",session);
#if defined(_WIN32) || defined(TOPSPIN_CHARACTER_WRAPPER_TEST)
 host_path(translated,"Media/Wads/Db.wad");
 CHECK(__wrap_xbox_translate_path("D:\\Media\\Wads\\Db.wad",host,2048));CHECK(!wcscmp(host,expected));
#ifdef TOPSPIN_CHARACTER_SDK_FILE_TEST
 CHECK(character_sdk_read("D:\\Media\\Wads\\Db.wad",replacement,sizeof replacement-1));
#endif
#endif
 const char *aliases[]={"D:\\Media\\Wads\\Db.wad","\\??\\D:\\Media/Wads/DB.WAD","\\Device\\CdRom0\\Media\\Wads\\Db.wad"};
 for(unsigned i=0;i<3;i++){host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[i],host,2048)==1);CHECK(!wcscmp(host,expected));}
 const char *unclaimed[]={"U:\\Media\\Wads\\Db.wad","T:\\Save.dat","D:\\TopSpin(Game).xbe","D:\\Media\\Wads\\Db.wad.extra","D:\\Media\\Wads\\Other.wad"};
 for(unsigned i=0;i<5;i++){host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(unclaimed[i],host,2048)==0);}
 const char *unsafe[]={"D:\\Media\\Wads\\..\\Wads\\Db.wad","D:\\Media\\Wads\\Db.wad:stream","D:\\Media\\Wads\\.\\Db.wad","D:\\Media\\Wads\\\\Db.wad"};
 for(unsigned i=0;i<4;i++){host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(unsafe[i],host,2048)==-1);}
 host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,4)==-1);
 host_path(host,"wrong.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==-1);
#ifdef _WIN32
 /* Verified objects and their ancestors cannot be replaced while held. */
 snprintf(p,sizeof p,"%s/session/files/0000.wad",root);HANDLE f=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f==INVALID_HANDLE_VALUE);CHECK(!DeleteFileA(p));
 snprintf(p,sizeof p,"%s/session/manifest.bin",root);f=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f==INVALID_HANDLE_VALUE);CHECK(!DeleteFileA(p));
 snprintf(p,sizeof p,"%s/assets/Media/Wads/Db.wad",root);f=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f==INVALID_HANDLE_VALUE);CHECK(!DeleteFileA(p));
 snprintf(p,sizeof p,"%s/session/files",root);char moved[2048];snprintf(moved,sizeof moved,"%s/session/moved",root);CHECK(!MoveFileA(p,moved));
#endif
 topspin_windows_character_session_close();pinned(digest);host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==1);
 topspin_windows_character_session_close();char bad_digest[65];memset(bad_digest,'0',64);bad_digest[64]=0;pinned(bad_digest);host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==-1);CHECK(topspin_windows_character_route(unclaimed[0],host,2048)==0);CHECK(topspin_windows_character_route(unclaimed[4],host,2048)==0);
 topspin_windows_character_session_close();pinned(digest);environment("MAX_SPIN_CHARACTER_ASSETS",NULL);host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==-1);
 topspin_windows_character_session_close();pinned(digest);memcpy(manifest,"MSCHAR1\0",8);used=8;number(0,4);CHECK(save());host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==-1);
 topspin_windows_character_session_close();fresh("Media/Wads/Db.wad");CHECK(save());
 topspin_windows_character_session_close();CHECK(write_bytes("session/files/0000.wad","changed",7));CHECK(!topspin_windows_character_session_validate(assets,session,digest));CHECK(write_bytes("session/files/0000.wad",replacement,sizeof replacement-1));
 CHECK(write_bytes("assets/Media/Wads/Db.wad","changed",7));CHECK(!topspin_windows_character_session_validate(assets,session,digest));CHECK(write_bytes("assets/Media/Wads/Db.wad",original,sizeof original-1));
 const char *invalid[]={"Media/Wads/Players/Sampras02.wad","Media/Wads/Players/Unknown00.wad","Media/Wads/../Wads/Db.wad","Media/Wads/Db.wad:stream","Media/Wads/Db.wad/","Media\\Wads\\Db.wad"};
 for(unsigned i=0;i<6;i++){fresh(invalid[i]);CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));}
 fresh("Media/Wads/Db.wad");manifest[8]=2;record("media/wads/db.wad");CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 fresh("Media/Wads/Db.wad");manifest[used++]=0;CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 fresh("Media/Wads/Db.wad");used--;CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 fresh("Media/Wads/Db.wad");manifest[8]=97;CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 fresh("Media/Wads/Db.wad");manifest[12]=241;CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 fresh("Media/Wads/Db.wad");manifest[14+strlen("Media/Wads/Db.wad")+3]=9;CHECK(save());CHECK(!topspin_windows_character_session_validate(assets,session,digest));
 /* All 32 identities and both outfits are native allowlist entries. */
 const char *names[]={"Sampras","HewittX","Gambill","GrosJea","Kuerten","ChangXX","BlakeXX","Robredo","TukurXX","Houston","WangXXX","KoenigX","GideXXX","Riosxxx","Emoryxx","NashXXX","HingisX","AnnaXXX","Barbara","Daniela","Dementi","Coetzer","Harkler","Shaughn","Yamanix","Baretto","Tellawi","Abercro","Auyeung","Hammond","sergeye","Hillxxx"};
 for(unsigned i=0;i<32;i++)for(unsigned j=0;j<2;j++){char resource[128],leaf[256],guest[160];snprintf(resource,sizeof resource,"Media/Wads/Players/%s%02u.wad",names[i],j);snprintf(leaf,sizeof leaf,"assets/%s",resource);CHECK(write_bytes(leaf,original,sizeof original-1));fresh(resource);CHECK(save());CHECK(topspin_windows_character_session_validate(assets,session,digest));host_path(host,resource);snprintf(guest,sizeof guest,"D:/%s",resource);CHECK(topspin_windows_character_route(guest,host,2048)==1);topspin_windows_character_session_close();snprintf(p,sizeof p,"%s/%s",root,leaf);CHECK(remove(p)==0);}
 fresh("Media/Wads/GameIni.wad");CHECK(write_bytes("assets/Media/Wads/GameIni.wad",original,sizeof original-1));CHECK(save());CHECK(topspin_windows_character_session_validate(assets,session,digest));topspin_windows_character_session_close();
 /* Zero-entry sessions are valid and preserve defaults. */
 memcpy(manifest,"MSCHAR1\0",8);used=8;number(0,4);CHECK(save());CHECK(topspin_windows_character_session_validate(assets,session,digest));host_path(host,"Media/Wads/Db.wad");CHECK(topspin_windows_character_route(aliases[0],host,2048)==0);topspin_windows_character_session_close();
#ifdef _WIN32
 fresh("Media/Wads/Db.wad");CHECK(save());wchar_t files[2048],real[2048];swprintf(files,2048,L"%ls/files",session);swprintf(real,2048,L"%ls/real-files",session);
 CHECK(MoveFileW(files,real));CHECK(junction(files,real));CHECK(!topspin_windows_character_session_validate(assets,session,digest));CHECK(RemoveDirectoryW(files));CHECK(MoveFileW(real,files));
#else
 fresh("Media/Wads/Db.wad");CHECK(save());CHECK(write_bytes("session/payload-real.wad",replacement,sizeof replacement-1));
 snprintf(p,sizeof p,"%s/session/files/0000.wad",root);CHECK(remove(p)==0);CHECK(symlink("../payload-real.wad",p)==0);CHECK(!topspin_windows_character_session_validate(assets,session,digest));CHECK(remove(p)==0);CHECK(write_bytes("session/files/0000.wad",replacement,sizeof replacement-1));
 snprintf(p,sizeof p,"%s/session/payload-real.wad",root);CHECK(remove(p)==0);
 char files[2048],real[2048];snprintf(files,sizeof files,"%s/session/files",root);snprintf(real,sizeof real,"%s/session/real-files",root);
 CHECK(rename(files,real)==0);CHECK(symlink("real-files",files)==0);CHECK(!topspin_windows_character_session_validate(assets,session,digest));CHECK(remove(files)==0);CHECK(rename(real,files)==0);
#endif
 topspin_windows_character_session_close();const char *cleanup_files[]={"session/files/0000.wad","session/manifest.bin","assets/Media/Wads/Db.wad","assets/Media/Wads/GameIni.wad"};for(unsigned i=0;i<4;i++){snprintf(p,sizeof p,"%s/%s",root,cleanup_files[i]);CHECK(remove(p)==0);}
 const char *dirs[]={"session/files","session","assets/Media/Wads/Players","assets/Media/Wads","assets/Media","assets",""};for(unsigned i=0;i<7;i++){snprintf(p,sizeof p,"%s/%s",root,dirs[i]);
#ifdef _WIN32
 CHECK(_rmdir(p)==0);
#else
 CHECK(rmdir(p)==0);
#endif
 }
 puts("PASS synthetic native character manifest, hashes, exact routes, handoff authentication, bounds and isolation");return 0;
}
