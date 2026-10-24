/* Persistent X11/XWayland owner. No guest runtime or Vulkan device/swapchain.
 * The title children present directly into this same native window. djultra64. */
#include "window_host.h"
#include "graphics_settings.h"
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
typedef struct {SDL_Window*window;void*xlib;} Owner;
static int window_dimension(const char*s,int*out)
{
 unsigned n=0;if(!s||!*s)return 0;
 for(;*s;s++){if(*s<'0'||*s>'9')return 0;n=n*10u+(unsigned)(*s-'0');if(n>16384u)return 0;}
 *out=(int)n;return 1;
}
static int initial_window_size(int*w,int*h)
{
 const char*sw=getenv("TOPSPIN_WINDOW_WIDTH"),*sh=getenv("TOPSPIN_WINDOW_HEIGHT");
 if(!sw&&!sh){*w=1280;*h=720;return 1;}
 int width,height;if(!window_dimension(sw,&width)||!window_dimension(sh,&height))return 0;
 if(height!=720&&height!=900&&height!=1080&&height!=1440&&height!=1800&&height!=2160)return 0;
 if(width!=height/9*16)return 0;
 *w=width;*h=height;return 1;
}
/* A decorated window must fit the usable display before CreateWindow: a WM
 * can clamp width/height independently before SDL receives an aspect hint.
 * Keep a small frame margin; never upscale, resize or refocus at handoff. */
static int fit_initial_window_size(int requested_w,int requested_h,const SDL_Rect *usable,int *w,int *h)
{
 if(!usable||!w||!h||requested_w<=0||requested_h<=0
    ||usable->w<352||usable->h<244)return 0;
 int units=requested_w/16;
 if(units>requested_h/9)units=requested_h/9;
 if(units>(usable->w-32)/16)units=(usable->w-32)/16;
 if(units>(usable->h-64)/9)units=(usable->h-64)/9;
 if(units<20)return 0; /* smallest usable client320x180 */
 *w=units*16;*h=units*9;return 1;
}
static int start(void*u,TopspinWindowInfo*i)
{
 Owner*o=u;
 int fullscreen=topspin_graphics_session_window_mode()==TOPSPIN_WINDOW_FULLSCREEN;
 int initial_width,initial_height;
 if(!initial_window_size(&initial_width,&initial_height)){fputs("[WINDOW-OWNER] invalid initial window size\n",stderr);return 0;}
 SDL_SetAppMetadata("Max Spin","0.1.0","org.djultra64.maxspin");
 SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER,"x11",SDL_HINT_OVERRIDE);
 /* This process owns WM/lifetime only. XI2 touch selection is exclusive per
  * native window, so leave it to the active title child. XSelectInput below
  * releases only core events and cannot release an XISelectEvents mask. */
 SDL_SetHintWithPriority("SDL_VIDEO_X11_XINPUT2","0",SDL_HINT_OVERRIDE);
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)){fprintf(stderr,"[WINDOW-OWNER] SDL: %s\n",SDL_GetError());return 0;}
 if(!SDL_GetCurrentVideoDriver()||strcmp(SDL_GetCurrentVideoDriver(),"x11")){fputs("[WINDOW-OWNER] X11/XWayland is required\n",stderr);return 0;}
 SDL_DisplayID display_id=SDL_GetPrimaryDisplay();SDL_Rect usable={0};
 int client_width,client_height;
 if(!display_id||!SDL_GetDisplayUsableBounds(display_id,&usable)
    ||!fit_initial_window_size(initial_width,initial_height,&usable,&client_width,&client_height)) {
  fprintf(stderr,"[WINDOW-OWNER] no usable16:9 startup client: %s\n",SDL_GetError());return 0;
 }
 fprintf(stderr,"[WINDOW-OWNER] requested client%dx%d; startup client%dx%d within usable%dx%d; internal raster is unchanged\n",initial_width,initial_height,client_width,client_height,usable.w,usable.h);
 SDL_WindowFlags flags=SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE;
 if(fullscreen)flags|=SDL_WINDOW_FULLSCREEN; /* SDL3: desktop resolution, no exclusive mode. */
 o->window=SDL_CreateWindow("Max Spin",client_width,client_height,flags);
 if(!o->window){fprintf(stderr,"[WINDOW-OWNER] Create: %s\n",SDL_GetError());return 0;}
 if(fullscreen){
  if(!SDL_SyncWindow(o->window)){fprintf(stderr,"[WINDOW-OWNER] Fullscreen sync: %s\n",SDL_GetError());return 0;}
 }else SDL_SetWindowAspectRatio(o->window,16.f/9.f,16.f/9.f);
 fprintf(stderr,"[WINDOW-OWNER] window mode=%s; one owner for every image\n",fullscreen?"fullscreen":"windowed");
 SDL_PropertiesID p=SDL_GetWindowProperties(o->window);
 Sint64 id=SDL_GetNumberProperty(p,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0);
 void*display=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_X11_DISPLAY_POINTER,NULL);
 int w=0,h=0;
 if(id<=0||(uint64_t)id>UINT32_MAX||!display||!SDL_GetWindowSizeInPixels(o->window,&w,&h)||w<=0||h<=0)return 0;
 /* Xlib's event masks are per connection. Retain structural/property/exposure
  * events; release the exclusive ButtonPress selection before children attach.
  * Load the ABI already used by SDL instead of needing X11 development headers. */
 o->xlib=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);
 if(!o->xlib){fputs("[WINDOW-OWNER] Cannot load X11 input-ownership adapter\n",stderr);return 0;}
 typedef int(*SelectInput)(void*,unsigned long,long);
 typedef int(*Sync)(void*,int);
 SelectInput select_input=(SelectInput)dlsym(o->xlib,"XSelectInput");
 Sync sync=(Sync)dlsym(o->xlib,"XSync");
 if(!select_input||!sync)return 0;
 select_input(display,(unsigned long)id,(1L<<17)|(1L<<22)|(1L<<15));
 sync(display,0);
 i->window=(uint32_t)id;i->width=(uint32_t)w;i->height=(uint32_t)h;
 /* Initial user launch only; no module transition ever reaches this again. */
 SDL_RaiseWindow(o->window);return 1;
}
static int events(void*u)
{
 Owner*o=u;SDL_Event e;
 while(SDL_PollEvent(&e))
  if(e.type==SDL_EVENT_QUIT||(e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED&&e.window.windowID==SDL_GetWindowID(o->window)))return 0;
 return 1;
}
static void delay(void*u){(void)u;SDL_Delay(8);}
static void stop(void*u)
{
 Owner*o=u;if(o->window)SDL_DestroyWindow(o->window);SDL_Quit();if(o->xlib)dlclose(o->xlib);
}
int main(void)
{
 signal(SIGPIPE,SIG_IGN);Owner owner={0};
 TopspinWindowHostOps ops={&owner,start,events,delay,stop};
 return topspin_window_host_run(&ops,STDIN_FILENO,STDOUT_FILENO);
}
