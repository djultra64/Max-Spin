/* Exact original disc resource selector. Own work: djultra64. */
#include "ui_layout_resources.h"
#include <stddef.h>
static unsigned normalized(unsigned c)
{if(c=='\\')return '/';return c>='A'&&c<='Z'?c+('a'-'A'):c;}
static int prefix(const char *path,const char *start)
{for(;*start;start++,path++)if(!*path||normalized((unsigned char)*path)!=normalized((unsigned char)*start))return 0;return 1;}
int topspin_ui_layout_disc_index(const char *path)
{
 if(!path)return -1;
 if(prefix(path,"\\??\\D:\\"))path+=7;
 else if(prefix(path,"\\Device\\CdRom0\\"))path+=15;
 else if(prefix(path,"D:\\"))path+=3;
 else return -1;
 for(unsigned i=0;i<sizeof topspin_ui_layout_resources/sizeof topspin_ui_layout_resources[0];i++){
  const char *known=topspin_ui_layout_resources[i].path,*p=path;
  while(*known&&*p&&normalized((unsigned char)*p)==normalized((unsigned char)*known)){p++;known++;}
  if(!*p&&!*known)return (int)i;
 }
 return -1;
}
