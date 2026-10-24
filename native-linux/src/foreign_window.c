/* Parse host-owned X11 identity; no video initialization or guest state.
 * Own work: djultra64. */
#include "foreign_window.h"
#include <stdio.h>
#include <string.h>
static int fail(char*error,size_t n,const char*message){if(error&&n)snprintf(error,n,"%s",message);return 0;}
int topspin_foreign_window_parse(const char*text,const char*legacy,const char*modern,
 TopspinForeignWindow*out,char*error,size_t n)
{
 if(!out)return fail(error,n,"Missing foreign window output");
 TopspinForeignWindow candidate={0,0};
 if(!text){*out=candidate;if(error&&n)error[0]=0;return 1;}
 const char*driver=modern?modern:legacy;
 if(!driver||strcmp(driver,"x11"))return fail(error,n,"Parent X11 window requires effective SDL video driver x11");
 unsigned base=10,digits=0;uint32_t value=0;
 if(text[0]=='0'&&(text[1]=='x'||text[1]=='X')){base=16;text+=2;}
 for(;*text;text++){
  unsigned digit=*text>='0'&&*text<='9'?(unsigned)(*text-'0'):*text>='a'&&*text<='f'?(unsigned)(*text-'a'+10):*text>='A'&&*text<='F'?(unsigned)(*text-'A'+10):16;
  if(digit>=base||++digits>(base==16?8u:10u)||value>(UINT32_MAX-digit)/base)return fail(error,n,"Parent X11 window must be a nonzero unsigned32 decimal or hex ID");
  value=value*base+digit;
 }
 if(!digits||!value)return fail(error,n,"Parent X11 window must be a nonzero unsigned32 decimal or hex ID");
 candidate.enabled=1;candidate.xid=value;*out=candidate;if(error&&n)error[0]=0;return 1;
}
