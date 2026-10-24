/* Private all-or-nothing original Save directory. Own work: djultra64.
 * No title data, Career flags, signatures or metadata are synthesized. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "material_save_transaction.h"
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
extern int xbox_SaveTransactionIOVersion(void);
#define MAX_ENTRIES 4096u
#define MAX_BYTES (128u*1024u*1024u)
#define MAX_TRANSACTIONS 16u
static struct {pthread_mutex_t mutex;int configured,active,failed,normal;unsigned attempts;char root[PATH_MAX],stage[PATH_MAX],temporary[PATH_MAX],workspace[PATH_MAX],runtime[PATH_MAX],runtime_workspace[PATH_MAX];uint64_t before;} tx={.mutex=PTHREAD_MUTEX_INITIALIZER};
static int child(const char *p,const char *root){size_t n=strlen(root);return !strncmp(p,root,n)&&(!p[n]||p[n]=='/');}
static int canonical(const char *p){if(!p||p[0]!='/')return 0;for(const char*s=p+1;*s;){const char*e=strchr(s,'/');size_t n=e?(size_t)(e-s):strlen(s);if(!n||(n==1&&s[0]=='.')||(n==2&&s[0]=='.'&&s[1]=='.'))return 0;if(!e)break;s=e+1;if(!*s)return 0;}return 1;}
/* Xbox partition translation can emit Cache//TmpStorage. Collapse separators
 * only; never resolve dot components, links or paths outside the given root. */
static int normalize(const char*p,size_t capacity,char out[PATH_MAX]){
 if(!p||!capacity||p[0]!='/'||strnlen(p,capacity)>=capacity)return 0;
 size_t used=1;out[0]='/';const char*s=p+1;
 while(*s){while(*s=='/')s++;if(!*s)break;const char*end=strchr(s,'/');size_t n=end?(size_t)(end-s):strlen(s);
  if((n==1&&s[0]=='.')||(n==2&&s[0]=='.'&&s[1]=='.')||used+n+1>=PATH_MAX)return 0;
  if(used>1)out[used++]='/';memcpy(out+used,s,n);used+=n;s+=n;
 }out[used]=0;return used<capacity;
}
static int temporary_path(const char*p){size_t n=strlen(tx.temporary),prefix=n-10;return !strncmp(p,tx.temporary,prefix)&&!strncasecmp(p+prefix,"TmpStorage",10)&&(!p[n]||p[n]=='/');}
static int runtime_path(const char*p){static const char tail[]="Media/PlayerCreatorPart/RunTimeParams";size_t n=strlen(tx.runtime),prefix=n-(sizeof tail-1);return !strncmp(p,tx.runtime,prefix)&&!strncasecmp(p+prefix,tail,sizeof tail-1)&&(!p[n]||p[n]=='/');}
static int join(char *p,const char*a,const char*b){int n=snprintf(p,PATH_MAX,"%s/%s",a,b);return n>0&&n<PATH_MAX;}
static int stable(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static void hash(uint64_t*h,const void*p,size_t n){const unsigned char*b=p;while(n--){*h^=*b++;*h*=1099511628211ull;}}
static void stat_hash(uint64_t*h,const struct stat*s){hash(h,&s->st_dev,sizeof s->st_dev);hash(h,&s->st_ino,sizeof s->st_ino);hash(h,&s->st_mode,sizeof s->st_mode);hash(h,&s->st_size,sizeof s->st_size);hash(h,&s->st_mtim,sizeof s->st_mtim);hash(h,&s->st_ctim,sizeof s->st_ctim);}
static int cmp(const void*a,const void*b){return strcmp(*(char*const*)a,*(char*const*)b);}
/* Source never followed through a link. A single ordered traversal supports
 * both initial copy and precommit identity/content-metadata revalidation. */
static int tree(int source,int destination,unsigned depth,unsigned*count,size_t*bytes,uint64_t*digest){
 if(depth>16||++*count>MAX_ENTRIES)return 0;
 struct stat before,after;if(fstat(source,&before)||!S_ISDIR(before.st_mode)||before.st_uid!=getuid()||(before.st_mode&022))return 0;stat_hash(digest,&before);
 int dupfd=openat(source,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dupfd<0)return 0;DIR*d=fdopendir(dupfd);if(!d){close(dupfd);return 0;}
 char**names=calloc(MAX_ENTRIES,sizeof*names);unsigned n=0;int ok=names!=NULL;struct dirent*e;
 errno=0;while(ok&&(e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;if(n==MAX_ENTRIES||!(names[n]=strdup(e->d_name))){ok=0;break;}n++;}if(errno)ok=0;closedir(d);
 if(ok)qsort(names,n,sizeof*names,cmp);
 for(unsigned i=0;ok&&i<n;i++){
  hash(digest,names[i],strlen(names[i])+1);struct stat a,b;if(++*count>MAX_ENTRIES||fstatat(source,names[i],&a,AT_SYMLINK_NOFOLLOW)||a.st_uid!=getuid()||(a.st_mode&022)){ok=0;break;}
  if(S_ISDIR(a.st_mode)){
   int in=openat(source,names[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),out=destination==-2?-2:-1;if(in<0){ok=0;break;}
   if(destination>=0){if(mkdirat(destination,names[i],0700)||(out=openat(destination,names[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC))<0)ok=0;}
   if(ok)ok=tree(in,out,depth+1,count,bytes,digest);if(out>=0){if(fsync(out))ok=0;close(out);}close(in);
  }else if(S_ISREG(a.st_mode)&&a.st_nlink==1&&a.st_size>=0&&(uint64_t)a.st_size<=MAX_BYTES-*bytes){
   *bytes+=(size_t)a.st_size;stat_hash(digest,&a);int in=openat(source,names[i],O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC),out=-1;
   if(in<0||fstat(in,&b)||!stable(&a,&b))ok=0;
   if(ok&&destination>=0&&(out=openat(destination,names[i],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600))<0)ok=0;
   unsigned char buffer[16384];off_t total=0;while(ok&&destination>=0){ssize_t got=read(in,buffer,sizeof buffer);if(got<0&&errno==EINTR)continue;if(got<0){ok=0;break;}if(!got)break;total+=got;size_t at=0;while(at<(size_t)got){ssize_t put=write(out,buffer+at,(size_t)got-at);if(put<0&&errno==EINTR)continue;if(put<=0){ok=0;break;}at+=(size_t)put;}}
   if(ok&&destination>=0&&total!=a.st_size)ok=0;if(in>=0){if(destination==-2&&fsync(in))ok=0;if(fstat(in,&b)||!stable(&a,&b))ok=0;close(in);}if(out>=0){if(fsync(out))ok=0;close(out);}
  }else ok=0;
 }
 for(unsigned i=0;i<n;i++)free(names[i]);free(names);if(fstat(source,&after)||!stable(&before,&after))ok=0;if(destination==-2&&fsync(source))ok=0;return ok;
}
static int inspect_budget(const char*source,const char*destination,uint64_t*digest,unsigned*count,size_t*bytes){int in=open(source,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),out=-1;if(in<0)return 0;if(destination&&!*destination)out=-2;else if(destination&&(out=open(destination,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC))<0){close(in);return 0;}*digest=14695981039346656037ull;int ok=tree(in,out,0,count,bytes,digest);if(out>=0){if(fsync(out))ok=0;close(out);}close(in);return ok;}
static int inspect(const char*source,const char*destination,uint64_t*digest){unsigned count=0;size_t bytes=0;return inspect_budget(source,destination,digest,&count,&bytes);}

static int owned_directory(const char*path,int create){
 struct stat st;char actual[PATH_MAX];
 if(create&&mkdir(path,0700)&&errno!=EEXIST)return 0;
 return !lstat(path,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&!(st.st_mode&022)&&realpath(path,actual)&&!strcmp(path,actual);
}
/* Delete only the three directories created by this operation, using owned
 * directory handles and never following links. The published title is separate. */
static int discard_contents(int fd,unsigned depth){
 if(depth>16)return 0;int copy=openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0)return 0;
 DIR*d=fdopendir(copy);if(!d){close(copy);return 0;}struct dirent*e;int ok=1;
 while(ok&&(e=readdir(d))){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  struct stat st;if(fstatat(fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)||st.st_uid!=getuid()){ok=0;break;}
  if(S_ISDIR(st.st_mode)){
   int childfd=openat(fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
   if(childfd<0){ok=0;break;}ok=discard_contents(childfd,depth+1);close(childfd);
   if(ok&&unlinkat(fd,e->d_name,AT_REMOVEDIR))ok=0;
  }else if(unlinkat(fd,e->d_name,0))ok=0;
 }closedir(d);return ok;
}
static void discard_scratch(const char*path){
 int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
 int ok=discard_contents(fd,0);close(fd);if(ok)(void)rmdir(path);
}
int mc_save_transaction_configure(const char*root){
 const char*version=getenv("TOPSPIN_CAREER_SAVE_VERSION");int normal=version&&!strcmp(version,"1");
 if(xbox_SaveTransactionIOVersion()!=2||tx.configured||!root||!canonical(root)||!owned_directory(root,0))return 0;
 char title[PATH_MAX],userdata[PATH_MAX];struct stat st;
 if(!normal){
  if(!strstr(root,"/reports/"))return 0;
  char parent[PATH_MAX];snprintf(parent,sizeof parent,"%s",root);char*slash=strrchr(parent,'/');if(!slash||strcmp(slash,"/saves"))return 0;*slash=0;if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0777)!=0700)return 0;
 }
 if(!join(userdata,root,"UserData")||!owned_directory(userdata,normal)||!join(title,userdata,"4d530035")||!owned_directory(title,normal))return 0;
 if(!join(tx.temporary,root,"Cache/TmpStorage")||!join(tx.runtime,root,"Cache/Media/PlayerCreatorPart/RunTimeParams"))return 0;
 strcpy(tx.root,title);tx.normal=normal;tx.configured=1;return 1;
}
int mc_save_transaction_begin(void){pthread_mutex_lock(&tx.mutex);int ok=0;
 if(!tx.configured||tx.active||(!tx.normal&&tx.attempts>=MAX_TRANSACTIONS))goto done;
 unsigned copied_entries=0;size_t copied_bytes=0;
 if(tx.attempts<UINT_MAX)tx.attempts++;int n=snprintf(tx.stage,sizeof tx.stage,"%s.material-save-XXXXXX",tx.root);if(n<0||n>=PATH_MAX||!mkdtemp(tx.stage))goto done;
 if(!inspect_budget(tx.root,tx.stage,&tx.before,&copied_entries,&copied_bytes))goto done;
 /* Original11A2D0 moves source files to Z:\\TmpStorage BEFORE writing Pack.
  * A distinct empty workspace supplies that exact temporary namespace without
  * modifying the original cache or packing temporary files into UserData. */
 n=snprintf(tx.workspace,sizeof tx.workspace,"%s.material-work-XXXXXX",tx.root);if(n<0||n>=PATH_MAX||!mkdtemp(tx.workspace))goto done;
 /* Original C5480 writes the runtime ADN, and C63F0 reads it back during
  * the SAME phase. Keep this namespace independent from Pack scratch and
  * seed it from the current cache without ever changing that cache. */
 n=snprintf(tx.runtime_workspace,sizeof tx.runtime_workspace,"%s.material-runtime-XXXXXX",tx.root);if(n<0||n>=PATH_MAX||!mkdtemp(tx.runtime_workspace))goto done;
 struct stat runtime_stat;
 if(!lstat(tx.runtime,&runtime_stat)){
  char actual[PATH_MAX];uint64_t ignored;
  if(!S_ISDIR(runtime_stat.st_mode)||!realpath(tx.runtime,actual)||strcmp(actual,tx.runtime)||
     !inspect_budget(tx.runtime,tx.runtime_workspace,&ignored,&copied_entries,&copied_bytes))goto done;
 }else if(errno!=ENOENT)goto done;
 tx.failed=0;tx.active=1;ok=1;
 done:pthread_mutex_unlock(&tx.mutex);return ok;
}
int xbox_save_path_transaction(char*path,size_t capacity){pthread_mutex_lock(&tx.mutex);int ok=1;if(tx.active){char clean[PATH_MAX],mapped[PATH_MAX];if(!normalize(path,capacity,clean)){tx.failed=1;ok=0;}else{const char*base=NULL,*suffix=NULL;if(child(clean,tx.root)){base=tx.stage;suffix=clean+strlen(tx.root);}else if(temporary_path(clean)){base=tx.workspace;suffix=clean+strlen(tx.temporary);}else if(runtime_path(clean)){base=tx.runtime_workspace;suffix=clean+strlen(tx.runtime);}int n=base?snprintf(mapped,sizeof mapped,"%s%s",base,suffix):snprintf(mapped,sizeof mapped,"%s",clean);if(n<0||(size_t)n>=sizeof mapped||(size_t)n>=capacity){tx.failed=1;ok=0;}else memcpy(path,mapped,(size_t)n+1);}}pthread_mutex_unlock(&tx.mutex);return ok;}
int xbox_save_mutation_transaction(int fd,const char*path){pthread_mutex_lock(&tx.mutex);int ok=1;if(tx.active){char actual[PATH_MAX];if(fd>=0){char link[64];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,actual,sizeof actual-1);if(n<0||n>=(ssize_t)sizeof actual-1){ok=0;}else{actual[n]=0;path=actual;}}if(ok&&(!path||!canonical(path)||(!child(path,tx.stage)&&!child(path,tx.workspace)&&!child(path,tx.runtime_workspace))))ok=0;if(!ok)tx.failed=1;}pthread_mutex_unlock(&tx.mutex);return ok;}
static int open_stage_handles(void){DIR*d=opendir("/proc/self/fd");if(!d)return 1;struct dirent*e;int found=0;while((e=readdir(d))){char p[PATH_MAX],link[PATH_MAX];int n=snprintf(link,sizeof link,"/proc/self/fd/%s",e->d_name);if(n<0||n>=PATH_MAX){found=1;break;}ssize_t size=readlink(link,p,sizeof p-1);if(size<0)continue;p[size]=0;if(child(p,tx.stage)||child(p,tx.workspace)||child(p,tx.runtime_workspace)){found=1;break;}}closedir(d);return found;}
int mc_save_transaction_failed(void){pthread_mutex_lock(&tx.mutex);int failed=tx.failed;pthread_mutex_unlock(&tx.mutex);return failed;}
int mc_save_transaction_end(int success){pthread_mutex_lock(&tx.mutex);int ok=0;if(!tx.active)goto done;
 unsigned final_entries=0;size_t final_bytes=0;uint64_t now,stage_digest,runtime_digest;if(success&&!tx.failed&&!open_stage_handles()&&inspect(tx.root,NULL,&now)&&now==tx.before&&inspect_budget(tx.stage,"",&stage_digest,&final_entries,&final_bytes)&&inspect_budget(tx.runtime_workspace,"",&runtime_digest,&final_entries,&final_bytes)){
  /* Linux exchange is the single commit point. No remove-then-rename gap, no
   * per-file partial publication, no fallback that could discard the old save. */
  char parent[PATH_MAX];strcpy(parent,tx.root);char*slash=strrchr(parent,'/');*slash=0;
  int directory=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(directory>=0){
   if(!fsync(directory)&&!syscall(SYS_renameat2,AT_FDCWD,tx.root,AT_FDCWD,tx.stage,RENAME_EXCHANGE)){
    ok=1;/* Publication already occurred. Never report a rollback after commit. */
    if(fsync(directory))fputs("[CREATOR-MATERIAL] committed Save; parent durability sync failed\n",stderr);
   }
   close(directory);
  }
 }
 tx.active=0;tx.failed=!ok;
 if(tx.normal){
  fprintf(stderr,"[CREATOR-MATERIAL] Save transaction %s; complete title publication=%d\n",ok?"committed":"rejected",ok);
  discard_scratch(tx.workspace);discard_scratch(tx.runtime_workspace);discard_scratch(tx.stage);
 }else{
  fprintf(stderr,"[CREATOR-MATERIAL] private Save transaction %s; retained %s at %s\n",ok?"committed":"rejected",ok?"original backup":"unpublished stage",tx.stage);
  fprintf(stderr,"[CREATOR-MATERIAL] retained private Pack workspace at %s\n",tx.workspace);
  fprintf(stderr,"[CREATOR-MATERIAL] retained private RuntimeParams workspace at %s\n",tx.runtime_workspace);
 }
 done:pthread_mutex_unlock(&tx.mutex);return ok;
}
