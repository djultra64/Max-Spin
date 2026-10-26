/* Win32 CRT quoting preserves each supplied argument exactly. djultra64. */
#include "character_command.h"
static int put(wchar_t *out,size_t cap,size_t *used,wchar_t value)
{
 if(*used>=cap-1)return 0;
 out[(*used)++]=value;return 1;
}
int topspin_windows_character_command(wchar_t *out,size_t cap,unsigned count,const wchar_t *const *args)
{
 if(!out||!cap)return 0;
 out[0]=0;if(!count||!args)return 0;
 size_t used=0;
 for(unsigned i=0;i<count;i++){
  if(!args[i]||(i&&!put(out,cap,&used,L' '))||!put(out,cap,&used,L'"'))goto fail;
  const wchar_t *p=args[i];
  for(;;){
   size_t slashes=0;
   while(*p==L'\\'){if(++slashes>=cap)goto fail;p++;}
   size_t escaped=slashes;
   if(!*p||*p==L'"')escaped=slashes*2+(*p==L'"');
   while(escaped--)if(!put(out,cap,&used,L'\\'))goto fail;
   if(!*p)break;
   if(!put(out,cap,&used,*p++))goto fail;
  }
  if(!put(out,cap,&used,L'"'))goto fail;
 }
 out[used]=0;return 1;
fail:
 out[0]=0;return 0;
}
