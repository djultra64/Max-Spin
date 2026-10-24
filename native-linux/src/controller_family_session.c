/* POSIX per-supervisor-session prompt preference. Own work: djultra64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "controller_family_session.h"
#include "controller_actions.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int fail(char*error,size_t size,const char*format,...){if(error&&size){va_list ap;va_start(ap,format);vsnprintf(error,size,format,ap);va_end(ap);}return 0;}
static int good(char*error,size_t size){if(error&&size)error[0]=0;return 1;}
static const char*token(int f){switch(f){case INPUT_FAMILY_AUTO:return "auto";case INPUT_FAMILY_XBOX:return "xbox";case INPUT_FAMILY_PLAYSTATION:return "playstation";case INPUT_FAMILY_NINTENDO:return "nintendo";default:return NULL;}}
int topspin_controller_family_load(int*out,char*error,size_t size){
 if(!out)return fail(error,size,"Missing controller family output");
 const char*path=getenv("TOPSPIN_CONTROLLER_SESSION_FILE");int family;
 if(!path){family=topspin_family_parse(getenv("TOPSPIN_CONTROLLER_FAMILY"));}
 else{
  if(!*path||strlen(path)>4096)return fail(error,size,"Invalid controller session path");
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return fail(error,size,"Cannot open controller session: %s",strerror(errno));
  struct stat st;char text[32];ssize_t got;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0600||st.st_size<2||st.st_size>16){close(fd);return fail(error,size,"Controller session must be a private bounded regular file");}
  do{got=read(fd,text,sizeof text);}while(got<0&&errno==EINTR);
  int closed=close(fd);if(got!=st.st_size||closed||text[got-1]!='\n'||memchr(text,0,(size_t)got))return fail(error,size,"Invalid controller session contents");
  text[got-1]=0;family=topspin_family_parse(text);
  if(!token(family)||strcmp(text,token(family)))return fail(error,size,"Controller session requires a canonical token");
 }
 if(!token(family))return fail(error,size,"Invalid controller family preference");
 *out=family;return good(error,size);
}
int topspin_controller_family_change(int previous,int requested,int(*apply)(int),char*error,size_t size){
 const char*value=token(requested);if(!token(previous)||!value||!apply)return fail(error,size,"Invalid controller family change");
 const char*path=getenv("TOPSPIN_CONTROLLER_SESSION_FILE");
 if(!path){if(!apply(requested))return fail(error,size,"Controller input is unavailable");return good(error,size);}
 int current;if(!topspin_controller_family_load(&current,error,size))return 0;
 (void)current; /* UI's previous is the actual backend preference to restore. */
 char temporary[4200];if(snprintf(temporary,sizeof temporary,"%s.tmp.XXXXXX",path)>=(int)sizeof temporary)return fail(error,size,"Controller session path too long");
 int fd=mkostemp(temporary,O_CLOEXEC);if(fd<0)return fail(error,size,"Cannot prepare controller session: %s",strerror(errno));
 char text[32];int count=snprintf(text,sizeof text,"%s\n",value);size_t at=0;int saved=0;
 while(at<(size_t)count){ssize_t n=write(fd,text+at,(size_t)count-at);if(n<0&&errno==EINTR)continue;if(n<=0){saved=n<0?errno:EIO;break;}at+=(size_t)n;}
 if(!saved&&fsync(fd))saved=errno;
 if(close(fd)&&!saved)saved=errno;
 if(saved){unlink(temporary);return fail(error,size,"Cannot write controller session: %s",strerror(saved));}
 if(!apply(requested)){unlink(temporary);return fail(error,size,"Controller input is unavailable");}
 if(rename(temporary,path)){
  saved=errno;unlink(temporary);int restored=apply(previous);
  return fail(error,size,"Cannot commit controller session: %s%s",strerror(saved),restored?"":" (input rollback failed)");
 }
 return good(error,size);
}
