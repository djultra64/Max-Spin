#include "host_windows.h"
#include <windows.h>
#include <io.h>
#include <errno.h>
#include "d3d8_xbox.h"
#include "sprite_ui_admission.h"
void guest_turn_set_code(const void*a,size_t b){(void)a;(void)b;}
void xbox_HostInputFocus(int a){(void)a;}void xbox_HostInputKey(int a,int b){(void)a;(void)b;}
IDirect3DDevice8*xbox_GetD3DDevice(void){return NULL;}
static const void*map(void*u,uint32_t a,size_t n){(void)u;(void)a;(void)n;return NULL;}
int main(int argc,char**argv){if(argc!=2)return 2;int dir=open(argv[1],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);printf("dirfd=%d errno=%d winerr=%lu\n",dir,errno,(unsigned long)GetLastError());if(dir<0)return 1;struct stat info;int result=fstat(dir,&info);printf("stat=%d mode=%o uid=%u inode=%llu\n",result,info.st_mode,info.st_uid,(unsigned long long)info.st_ino);const char*names[]={"pc_cross.xpr","readystart.xpr","shop_slider.xpr"};for(unsigned i=0;i<3;i++){int f=openat(dir,names[i],O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);printf("file=%s fd=%d errno=%d winerr=%lu\n",names[i],f,errno,(unsigned long)GetLastError());if(f<0)return 1;result=fstat(f,&info);printf("stat=%d mode=%o uid=%u size=%lld inode=%llu\n",result,info.st_mode,info.st_uid,(long long)info.st_size,(unsigned long long)info.st_ino);unsigned char data[8192];int n=read(f,data,sizeof data);printf("read=%d first=%02x%02x%02x%02x\n",n,data[0],data[1],data[2],data[3]);close(f);}close(dir);result=sxui_configure(argv[1],"menu",map,NULL);printf("admission=%d\n",result);return result?0:1;}
