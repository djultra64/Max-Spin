/* One owner window, independent of the sequential title modules. djultra64. */
#ifndef TOPSPIN_WINDOW_HOST_H
#define TOPSPIN_WINDOW_HOST_H
#include <stdint.h>
typedef struct {uint32_t window,width,height;} TopspinWindowInfo;
typedef struct {
 void*user;
 int(*start)(void*,TopspinWindowInfo*);
 /* 1continue,0user close,-1error. Event pump never renders a second swapchain. */
 int(*events)(void*);
 void(*delay)(void*);
 void(*stop)(void*);
} TopspinWindowHostOps;
int topspin_window_host_run(const TopspinWindowHostOps*,int control_fd,int ready_fd);
#endif
