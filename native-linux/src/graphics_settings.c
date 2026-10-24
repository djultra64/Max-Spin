/* Strict bounded INI and atomic preference commit; no renderer coupling. djultra64. */
#include "graphics_settings.h"
#include "graphics_settings_io.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static const uint32_t heights[]={720,900,1080,1440,1800,2160};
static const uint32_t caps[]={60,90,120,0};
static const char*const aa_names[]={"guest","off","msaa2","msaa4","msaa8","ssaa2x2"};
static const char*const window_names[]={"windowed","fullscreen"};
static const char*const filter_names[]={"original","superxbr2x","superxbr4x"};
static int failure(char*error,size_t n,const char*format,...)
{if(error&&n){va_list ap;va_start(ap,format);vsnprintf(error,n,format,ap);va_end(ap);}return 0;}
static int success(char*error,size_t n){if(error&&n)error[0]=0;return 1;}
static int index_of(uint32_t value,const uint32_t*list,size_t n)
{for(size_t i=0;i<n;i++)if(value==list[i])return (int)i;return -1;}
void topspin_graphics_settings_defaults(TopspinGraphicsSettings*s)
{if(s)*s=(TopspinGraphicsSettings){1,720,TOPSPIN_AA_GUEST,TOPSPIN_FILTER_ORIGINAL,60,TOPSPIN_WINDOW_WINDOWED};}
void topspin_graphics_capabilities_defaults(TopspinGraphicsCapabilities*c)
{if(c)*c=(TopspinGraphicsCapabilities){1,1u<<TOPSPIN_AA_GUEST,1u<<TOPSPIN_FILTER_ORIGINAL,1,{1u<<TOPSPIN_AA_GUEST,0,0,0,0,0}};}
uint32_t topspin_graphics_resolution_width(uint32_t height)
{return index_of(height,heights,6)<0?0:height/9*16;}
int topspin_graphics_settings_validate(const TopspinGraphicsSettings*s,char*error,size_t n)
{
 if(!s)return failure(error,n,"missing settings");
 if(s->version!=1)return failure(error,n,"unsupported version");
 if(!topspin_graphics_resolution_width(s->resolution_height))return failure(error,n,"invalid resolution_height");
 if((unsigned)s->aa>=TOPSPIN_AA_COUNT)return failure(error,n,"invalid aa");
 if((unsigned)s->filter2d>=TOPSPIN_FILTER_COUNT)return failure(error,n,"invalid filter2d");
 if((unsigned)s->window_mode>=TOPSPIN_WINDOW_MODE_COUNT)return failure(error,n,"invalid window_mode");
 if(index_of(s->frame_cap,caps,4)<0)return failure(error,n,"invalid frame_cap");
 return success(error,n);
}
int topspin_graphics_settings_supported(const TopspinGraphicsSettings*s,const TopspinGraphicsCapabilities*c,char*error,size_t n)
{
 if(!topspin_graphics_settings_validate(s,error,n))return 0;
 if(!c)return failure(error,n,"missing capabilities");
 if(!(c->resolutions&(1u<<index_of(s->resolution_height,heights,6))))return failure(error,n,"resolution is not supported by the active renderer");
 if(!(c->aa&(1u<<s->aa)))return failure(error,n,"aa is not supported by the active renderer");
 if(!(c->aa_by_resolution[index_of(s->resolution_height,heights,6)]&(1u<<s->aa)))return failure(error,n,"aa/resolution combination is not supported");
 if(!(c->filters&(1u<<s->filter2d)))return failure(error,n,"filter2d is not supported by the active renderer");
 if(!(c->frame_caps&(1u<<index_of(s->frame_cap,caps,4))))return failure(error,n,"frame_cap is not supported by the active presentation contract");
 return success(error,n);
}
static char*trim(char*p)
{while(*p==' '||*p=='\t'||*p=='\r')p++;char*end=p+strlen(p);while(end>p&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'))*--end=0;return p;}
static int number(const char*p,uint32_t*out)
{uint32_t value=0;if(!*p)return 0;for(;*p;p++){if(*p<'0'||*p>'9'||value>(UINT32_MAX-(unsigned)(*p-'0'))/10)return 0;value=value*10+(unsigned)(*p-'0');}*out=value;return 1;}
static int named(const char*p,const char*const*names,unsigned count)
{for(unsigned i=0;i<count;i++)if(!strcmp(p,names[i]))return (int)i;return -1;}
int topspin_graphics_settings_parse(const char*data,size_t bytes,TopspinGraphicsSettings*out,char*error,size_t n)
{
 if(!data||!out||!bytes||bytes>TOPSPIN_GRAPHICS_INI_LIMIT)return failure(error,n,"invalid INI size or arguments");
 if(memchr(data,0,bytes))return failure(error,n,"embedded NUL");
 for(size_t i=0;i<bytes;i++)if((unsigned char)data[i]<32&&data[i]!='\n'&&data[i]!='\r'&&data[i]!='\t')return failure(error,n,"invalid INI character");
 char buffer[TOPSPIN_GRAPHICS_INI_LIMIT+1];memcpy(buffer,data,bytes);buffer[bytes]=0;
 TopspinGraphicsSettings value={0};unsigned seen=0;char*line=buffer;
 while(line){char*next=strchr(line,'\n');if(next)*next++=0;
  if(strlen(line)>160)return failure(error,n,"INI line too long");
  char*key=trim(line);if(!*key||*key=='#'||*key==';'){line=next;continue;}
  char*equal=strchr(key,'=');if(!equal||strchr(equal+1,'='))return failure(error,n,"expected key=value");
  *equal=0;key=trim(key);char*text=trim(equal+1);unsigned bit;int enum_value;
  if(!strcmp(key,"version")){bit=1;if(!number(text,&value.version))return failure(error,n,"invalid version");}
  else if(!strcmp(key,"resolution_height")){bit=2;if(!number(text,&value.resolution_height))return failure(error,n,"invalid resolution_height");}
  else if(!strcmp(key,"aa")){bit=4;enum_value=named(text,aa_names,TOPSPIN_AA_COUNT);if(enum_value<0)return failure(error,n,"invalid aa");value.aa=(TopspinGraphicsAA)enum_value;}
  else if(!strcmp(key,"filter2d")){bit=8;enum_value=named(text,filter_names,TOPSPIN_FILTER_COUNT);if(enum_value<0)return failure(error,n,"invalid filter2d");value.filter2d=(TopspinGraphicsFilter)enum_value;}
  else if(!strcmp(key,"frame_cap")){bit=16;if(!number(text,&value.frame_cap))return failure(error,n,"invalid frame_cap");}
  else if(!strcmp(key,"window_mode")){bit=32;enum_value=named(text,window_names,TOPSPIN_WINDOW_MODE_COUNT);if(enum_value<0)return failure(error,n,"invalid window_mode");value.window_mode=(TopspinWindowMode)enum_value;}
  else return failure(error,n,"unknown INI key");
  if(seen&bit)return failure(error,n,"duplicate INI key");
  seen|=bit;line=next;
 }
 if((seen&31)!=31)return failure(error,n,"missing INI keys");
 if(!topspin_graphics_settings_validate(&value,error,n))return 0;
 *out=value;return success(error,n);
}
int topspin_graphics_settings_format(const TopspinGraphicsSettings*s,char*out,size_t bytes,char*error,size_t n)
{
 if(!topspin_graphics_settings_validate(s,error,n))return 0;
 char text[256];int size=snprintf(text,sizeof text,"version=1\nresolution_height=%u\naa=%s\nfilter2d=%s\nframe_cap=%u\nwindow_mode=%s\n",s->resolution_height,aa_names[s->aa],filter_names[s->filter2d],s->frame_cap,window_names[s->window_mode]);
 if(!out||size<0||(size_t)size>=sizeof text||bytes<=(size_t)size)return failure(error,n,"INI output buffer too small");
 memcpy(out,text,(size_t)size+1);return success(error,n);
}
int topspin_graphics_settings_apply(const char*path,const TopspinGraphicsSettings*requested,const TopspinGraphicsCapabilities*c,TopspinGraphicsSettings*active,char*error,size_t n)
{
 if(!active||!requested||!path)return failure(error,n,"missing commit arguments");
 TopspinGraphicsSettings value=*requested;
 if(!topspin_graphics_settings_supported(&value,c,error,n))return 0;
 char text[256];if(!topspin_graphics_settings_format(&value,text,sizeof text,error,n))return 0;
 if(!topspin_graphics_settings_store_file(path,text,error,n))return 0;
 *active=value;return success(error,n);
}
