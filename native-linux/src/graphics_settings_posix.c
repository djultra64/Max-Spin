/* POSIX-only file adapter. Windows requires a separately verified atomic
 * replacement adapter; this file is not a Windows implementation. djultra64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "graphics_settings.h"
#include "graphics_settings_io.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int failure(char*error,size_t n,const char*format,...)
{if(error&&n){va_list ap;va_start(ap,format);vsnprintf(error,n,format,ap);va_end(ap);}return 0;}
static int success(char*error,size_t n){if(error&&n)error[0]=0;return 1;}
int topspin_graphics_settings_load(const char*path,TopspinGraphicsSettings*out,char*error,size_t n)
{
 if(!path||!out)return failure(error,n,"missing path or output");
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return failure(error,n,"open: %s",strerror(errno));
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>TOPSPIN_GRAPHICS_INI_LIMIT){close(fd);return failure(error,n,"not a bounded regular INI file");}
 char text[TOPSPIN_GRAPHICS_INI_LIMIT+1];size_t used=0;
 while(used<sizeof text){ssize_t got=read(fd,text+used,sizeof text-used);if(got<0){if(errno==EINTR)continue;int e=errno;close(fd);return failure(error,n,"read: %s",strerror(e));}if(!got)break;used+=(size_t)got;}
 if(close(fd))return failure(error,n,"close: %s",strerror(errno));
 return topspin_graphics_settings_parse(text,used,out,error,n);
}
int topspin_graphics_settings_store_file(const char*path,const char*text,char*error,size_t n)
{
 char parent[4096],base[256];size_t length=strlen(path);if(!length||length>=sizeof parent)return failure(error,n,"invalid config path");
 const char*slash=strrchr(path,'/');const char*leaf=slash?slash+1:path;
 if(!*leaf||strlen(leaf)>=sizeof base||!strcmp(leaf,".")||!strcmp(leaf,".."))return failure(error,n,"invalid config filename");
 strcpy(base,leaf);if(!slash)strcpy(parent,".");else if(slash==path)strcpy(parent,"/");else{memcpy(parent,path,(size_t)(slash-path));parent[slash-path]=0;}
 int dir=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0)return failure(error,n,"open config directory: %s",strerror(errno));
 struct stat st;int existing=fstatat(dir,base,&st,AT_SYMLINK_NOFOLLOW);
 if((!existing&&!S_ISREG(st.st_mode))||(existing&&errno!=ENOENT)){close(dir);return failure(error,n,"config destination is not a regular file");}
 static atomic_uint sequence;char temporary[96];int fd=-1;unsigned attempt;
 for(attempt=0;attempt<32;attempt++){
  snprintf(temporary,sizeof temporary,".graphics-settings.tmp.%ld.%u",(long)getpid(),atomic_fetch_add(&sequence,1));
  fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
  if(fd>=0||errno!=EEXIST)break;
 }
 if(fd<0){int e=errno;close(dir);return failure(error,n,"create config temporary: %s",strerror(e));}
 const char*step="write";size_t written=0,total=strlen(text);
 while(written<total){ssize_t got=write(fd,text+written,total-written);if(got<0&&errno==EINTR)continue;if(got<=0){if(!got)errno=EIO;goto io_failure;}written+=(size_t)got;}
 step="fsync";while(fsync(fd)){if(errno!=EINTR)goto io_failure;}
 step="close";if(close(fd)){fd=-1;goto io_failure;}fd=-1;
 step="rename";if(renameat(dir,temporary,dir,base))goto io_failure;
 /* Rename is the commit point. A post-commit directory fsync is best effort;
  * never report rollback after the destination has already changed. */
 (void)fsync(dir);(void)close(dir);return success(error,n);
 io_failure:{int e=errno;if(fd>=0)(void)close(fd);(void)unlinkat(dir,temporary,0);(void)close(dir);return failure(error,n,"%s config: %s",step,strerror(e));}
}

/* Read the session copy, never newly saved preferences during a handoff. */
TopspinWindowMode topspin_graphics_session_window_mode(void)
{
 const char *path=getenv("TOPSPIN_SESSION_RENDER_INI");
 if(!path)path=getenv("TOPSPIN_GRAPHICS_INI");
 TopspinGraphicsSettings settings;topspin_graphics_settings_defaults(&settings);
 if(path&&*path)topspin_graphics_settings_load(path,&settings,NULL,0);
 return settings.window_mode;
}
