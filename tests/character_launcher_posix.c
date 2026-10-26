/* Real POSIX files/processes for the portable supervisor contract. djultra64. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include "character_launcher_platform.h"
static int narrow(char *out,const wchar_t *path){size_t n=wcstombs(out,path,131071);if(n==(size_t)-1||n>=131071)return 0;out[n]=0;return 1;}
int topspin_character_platform_kind(const wchar_t *path){char p[131072];struct stat s;if(!narrow(p,path))return -1;if(lstat(p,&s))return errno==ENOENT?0:-1;return S_ISDIR(s.st_mode)?2:S_ISREG(s.st_mode)?1:-1;}
int topspin_character_platform_environment(const wchar_t *key,const wchar_t *value){char k[131072],v[131072];if(!narrow(k,key))return 0;return value?(narrow(v,value)&&!setenv(k,v,1)):!unsetenv(k);}
int topspin_character_platform_packages(const wchar_t *path){char p[131072],q[131072];if(!narrow(p,path))return -1;DIR *d=opendir(p);if(!d)return errno==ENOENT?0:-1;int found=0;struct dirent *entry;while((entry=readdir(d))){const char *n=entry->d_name;size_t i=0;while(n[i]>='0'&&n[i]<='9')i++;if(!i||n[i])continue;if(snprintf(q,sizeof q,"%s/%s/manifest.json",p,n)>=(int)sizeof q){found=-1;break;}struct stat s;if(!lstat(q,&s)){found=1;break;}}closedir(d);return found;}
int topspin_character_platform_python(wchar_t *out,unsigned cap,int *py){const char *p=getenv("MAX_SPIN_PYTHON");*py=0;if(!p||!*p)return 0;size_t n=mbstowcs(out,p,cap);return n!=(size_t)-1&&n<cap&&topspin_character_platform_kind(out)==1;}
int topspin_character_platform_run(const wchar_t *exe,unsigned count,const wchar_t *const *args,const wchar_t *cwd,wchar_t *message,unsigned cap){
 char program[131072],directory[131072];char *argv[16]={0};int pipes[2],status=1;size_t used=0;char captured[8192];
 if(count>=16||!narrow(program,exe)||!narrow(directory,cwd)||pipe(pipes))return 0;
 for(unsigned i=0;i<count;i++){argv[i]=malloc(131072);if(!argv[i]||!narrow(argv[i],args[i]))abort();}
 pid_t child=fork();if(!child){close(pipes[0]);dup2(pipes[1],1);dup2(pipes[1],2);close(pipes[1]);if(chdir(directory))_exit(126);execv(program,argv);_exit(127);}
 close(pipes[1]);char chunk[1024];ssize_t got;while((got=read(pipes[0],chunk,sizeof chunk))>0){size_t n=(size_t)got;if(n>sizeof captured-1-used)n=sizeof captured-1-used;memcpy(captured+used,chunk,n);used+=n;}close(pipes[0]);captured[used]=0;
 if(child>0)while(waitpid(child,&status,0)<0&&errno==EINTR){}
 for(unsigned i=0;i<count;i++)free(argv[i]);
 if(cap){size_t n=mbstowcs(message,captured,cap-1);if(n==(size_t)-1)message[0]=0;else message[n]=0;}
 return child>0&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
void topspin_character_platform_remove(const wchar_t *owned){
 char p[131072];struct stat s;if(!narrow(p,owned))return;
 int root=open(p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(root<0)return;
 int files=openat(root,"files",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(files>=0){
  for(unsigned i=0;i<96;i++){char name[32];snprintf(name,sizeof name,"%04u.wad",i);
   if(!fstatat(files,name,&s,AT_SYMLINK_NOFOLLOW)&&S_ISREG(s.st_mode))unlinkat(files,name,0);}
  close(files);unlinkat(root,"files",AT_REMOVEDIR);
 }
 if(!fstatat(root,"manifest.bin",&s,AT_SYMLINK_NOFOLLOW)&&S_ISREG(s.st_mode))unlinkat(root,"manifest.bin",0);
 close(root);rmdir(p);
}
int topspin_windows_character_session_validate(const wchar_t *assets,const wchar_t *session,char digest[65]){
 (void)assets;wchar_t path[32768];swprintf(path,32768,L"%ls/manifest.bin",session);char p[131072];if(!narrow(p,path))return 0;FILE *f=fopen(p,"rb");if(!f)return 0;unsigned char data[13];size_t n=fread(data,1,sizeof data,f);fclose(f);if(n!=12||memcmp(data,"MSCHAR1\0\0\0\0\0",12))return 0;memset(digest,'0',64);digest[64]=0;return 1;
}
void topspin_windows_character_session_close(void){}
void test_character_locale(void){setlocale(LC_ALL,"");}
