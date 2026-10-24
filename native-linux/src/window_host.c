/* Lifecycle/IPC only. Own integration: djultra64. */
#include "window_host.h"
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
int topspin_window_host_run(const TopspinWindowHostOps*o,int control,int ready)
{
 if(!o||!o->start||!o->events||!o->delay||!o->stop)return 2;
 TopspinWindowInfo info={0};int status=2,closing=0;
 if(!o->start(o->user,&info)||!info.window||!info.width||!info.height||info.width>16384||info.height>16384)goto done;
 if(dprintf(ready,"{\"protocol\":1,\"window\":%u,\"driver\":\"x11\",\"width\":%u,\"height\":%u}\n",info.window,info.width,info.height)<0)goto done;
 for(;;){
  struct pollfd fd={control,POLLIN|POLLHUP,0};int result=poll(&fd,1,0);
  if(result<0){if(errno==EINTR)continue;break;}
  if(fd.revents&(POLLERR|POLLNVAL))break;
  if(fd.revents&(POLLIN|POLLHUP)){
   unsigned char command;ssize_t n=read(control,&command,1);
   if(n==0||(n==1&&command=='Q')){status=0;break;}
   if(n<0&&errno==EINTR)continue;
   break;
  }
  if(!closing){
   int action=o->events(o->user);
   if(action<0)break;
   if(action==0){
    /* Keep the native window alive until the supervisor has stopped its
     * presenting child and closes our control pipe. No SURFACE_LOST handoff. */
    if(dprintf(ready,"{\"event\":\"close\"}\n")<0)break;
    closing=1;
   }
  }else if(o->events(o->user)<0)break;
  o->delay(o->user);
 }
done:o->stop(o->user);return status;
}
