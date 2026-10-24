/* Native RUI model: no overlay, SDL input capture or renderer calls. djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "native_graphics.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static const unsigned heights[]={720,900,1080,1440,1800,2160};
static const char*const aa_labels[]={"GAME SETTINGS","OFF","MSAA 2X","MSAA 4X","MSAA 8X","SSAA 2X2"};
static const char*const filter_labels[]={"ORIGINAL","SUPER-XBR 2X","SUPER-XBR 4X"};
static int same(const TopspinGraphicsSettings*a,const TopspinGraphicsSettings*b){return a->resolution_height==b->resolution_height&&a->aa==b->aa&&a->filter2d==b->filter2d&&a->frame_cap==b->frame_cap&&a->window_mode==b->window_mode;}
static int available(TopspinNativeGraphics*m,const TopspinGraphicsSettings*s){return topspin_graphics_settings_validate(s,NULL,0)&&m->ready&&m->ready(s,m->user);}
int topspin_native_graphics_configure(TopspinNativeGraphics*m,const char*path,TopspinNativeGraphicsReady ready,void*user,unsigned height,TopspinGraphicsAA aa,TopspinGraphicsFilter filter){
 if(!m||m->configured||!path||!*path||strlen(path)>=sizeof m->path||!topspin_graphics_resolution_width(height)||(unsigned)aa>=TOPSPIN_AA_COUNT||(unsigned)filter>=TOPSPIN_FILTER_COUNT)return 0;
 TopspinNativeGraphics v={0};strcpy(v.path,path);v.ready=ready;v.user=user;
 topspin_graphics_settings_defaults(&v.running);v.running.resolution_height=height;v.running.aa=aa;v.running.filter2d=filter;v.running.window_mode=topspin_graphics_session_window_mode();v.saved=v.running;
 struct stat st;if(!lstat(path,&st)){
  if(!S_ISREG(st.st_mode)||!topspin_graphics_settings_load(path,&v.saved,NULL,0))strcpy(v.status,"INVALID PREFERENCES");
 }
 v.requested=v.saved;v.configured=1;*m=v;return 1;
}
void topspin_native_graphics_begin(TopspinNativeGraphics*m){
 if(!m||!m->configured)return;
 m->requested=m->saved;
 if(!available(m,&m->saved)){m->requested=m->running;strcpy(m->status,"UNSUPPORTED SETTINGS");return;}
 if(same(&m->saved,&m->running))snprintf(m->status,sizeof m->status,"RUNNING %u X %u",topspin_graphics_resolution_width(m->running.resolution_height),m->running.resolution_height);
 else strcpy(m->status,"RESTART TO APPLY");
}
int topspin_native_graphics_adjust(TopspinNativeGraphics*m,unsigned row,int direction){
 if(!m||!m->configured||row>4||(direction!=1&&direction!=-1))return 0;
 if(row==3){strcpy(m->status,"NOT AVAILABLE");return 0;}
 if(row==4){m->requested.window_mode=m->requested.window_mode==TOPSPIN_WINDOW_WINDOWED?TOPSPIN_WINDOW_FULLSCREEN:TOPSPIN_WINDOW_WINDOWED;strcpy(m->status,"SAVE AND RESTART");return 1;}
 unsigned count=row==0?6:row==1?TOPSPIN_AA_COUNT:TOPSPIN_FILTER_COUNT,index=0;
 if(row==0){while(index<count&&heights[index]!=m->requested.resolution_height)index++;}else index=row==1?(unsigned)m->requested.aa:(unsigned)m->requested.filter2d;
 if(index>=count)return 0;
 for(unsigned step=1;step<count;step++){
  unsigned next=(index+count+(direction>0?(int)step:-(int)step))%count;
  TopspinGraphicsSettings candidate=m->requested;
  if(row==0)candidate.resolution_height=heights[next];else if(row==1)candidate.aa=(TopspinGraphicsAA)next;else candidate.filter2d=(TopspinGraphicsFilter)next;
  /* Resolution is the user's choice. A saved SSAA multiplier must not hide
   * higher sizes: try the originalAA at that size before skipping it. Actual
   * device/readiness checks still decide admission; other settings stay put. */
  if(row==0&&!available(m,&candidate)&&candidate.aa!=TOPSPIN_AA_GUEST){
   candidate.aa=TOPSPIN_AA_GUEST;
   if(available(m,&candidate)){m->requested=candidate;strcpy(m->status,"AA RESET; SAVE AND RESTART");return 1;}
  }
  if(available(m,&candidate)){m->requested=candidate;strcpy(m->status,"SAVE AND RESTART");return 1;}
 }
 strcpy(m->status,"NO OTHER VALUE");return 0;
}
int topspin_native_graphics_value(const TopspinNativeGraphics*m,unsigned row,char*out,size_t n){
 if(!m||!m->configured||!out||!n||row>4)return 0;
 int used;
 if(row==0)used=snprintf(out,n,"%u X %u%s",topspin_graphics_resolution_width(m->requested.resolution_height),m->requested.resolution_height,m->requested.resolution_height==2160?" (4K)":"");
 else if(row==1)used=snprintf(out,n,"%s",aa_labels[m->requested.aa]);
 else if(row==2)used=snprintf(out,n,"%s",filter_labels[m->requested.filter2d]);
 else if(row==4)used=snprintf(out,n,"%s",m->requested.window_mode==TOPSPIN_WINDOW_FULLSCREEN?"FULLSCREEN":"WINDOWED");
 else if(m->requested.frame_cap)used=snprintf(out,n,"%u FPS",m->requested.frame_cap);
 else used=snprintf(out,n,"UNLIMITED");
 return used>=0&&(size_t)used<n;
}
int topspin_native_graphics_save(TopspinNativeGraphics*m){
 if(!m||!m->configured)return 0;
 if(!available(m,&m->requested)){strcpy(m->status,"NOT AVAILABLE");return 0;}
 TopspinGraphicsCapabilities caps;topspin_graphics_capabilities_defaults(&caps);
 caps.resolutions=63;caps.aa=(1u<<TOPSPIN_AA_COUNT)-1;caps.filters=(1u<<TOPSPIN_FILTER_COUNT)-1;caps.frame_caps=15;
 for(unsigned i=0;i<6;i++)caps.aa_by_resolution[i]=caps.aa;
 if(!topspin_graphics_settings_apply(m->path,&m->requested,&caps,&m->saved,NULL,0)){strcpy(m->status,"SAVE FAILED");return 0;}
 strcpy(m->status,same(&m->saved,&m->running)?"PREFERENCES SAVED":"SAVED; RESTART GAME");return 1;
}
