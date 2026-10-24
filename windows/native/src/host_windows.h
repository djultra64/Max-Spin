/* Native Windows host support. Own work: djultra64. */
#ifndef MAX_SPIN_HOST_WINDOWS_H
#define MAX_SPIN_HOST_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
struct topspin_windows_stat {uint64_t st_dev,st_ino;unsigned st_mode,st_uid;int64_t st_size;struct timespec st_mtim,st_ctim;};
int topspin_windows_stat(const char*,struct topspin_windows_stat*);
int topspin_windows_fstat(int,struct topspin_windows_stat*);
int topspin_windows_openat(int,const char*,int,...);
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x08000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0x10000000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0x20000000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x40000000
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#define S_ISDIR(m) (((m)&_S_IFMT)==_S_IFDIR)
#endif
int topspin_windows_fchmod(int,int);
int topspin_windows_open(const char*,int,...);
int topspin_windows_lstat(const char*,struct topspin_windows_stat*);
char *topspin_windows_realpath(const char*,char*);
struct D3D8VkHost;
char **topspin_windows_utf8_argv(int,wchar_t**);
int topspin_windows_poll_input(void);
void topspin_windows_backtrace(void);
int topspin_windows_register_guest_code(void);
uintptr_t topspin_windows_parent_window(void);
int topspin_windows_attach_drawable(uintptr_t,int*,int*);
void topspin_windows_vulkan_host(struct D3D8VkHost*,uintptr_t);
int topspin_windows_setenv(const char*,const char*,int);
FILE *topspin_windows_fopen(const char*,const char*);
#ifndef MAX_SPIN_HOST_WINDOWS_IMPLEMENTATION
#define setenv topspin_windows_setenv
#define fopen topspin_windows_fopen
#define open topspin_windows_open
#define stat topspin_windows_stat
#define fstat topspin_windows_fstat
#define openat topspin_windows_openat
#define lstat topspin_windows_lstat
#define realpath topspin_windows_realpath
#define getuid() 0
#define geteuid() 0
#define fsync _commit
#define fchmod topspin_windows_fchmod
#endif
#endif
