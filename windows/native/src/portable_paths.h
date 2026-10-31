/* Portable native paths; shared with CPU contracts. Own work: djultra64. */
#ifndef MAXSPIN_PORTABLE_PATHS_H
#define MAXSPIN_PORTABLE_PATHS_H
#include <stddef.h>
#include <wchar.h>
#include <wctype.h>
/* Restrict Win32 aliases before touching the filesystem. Dot segments, device
 * names, alternate streams and trailing dots/spaces cannot bypass containment. */
static int topspin_portable_normalize(wchar_t *out,unsigned cap,const wchar_t *input)
{
 size_t n=input?wcslen(input):0;
 if(!out||!cap)return 0;
 out[0]=0;
 if(n<3||n>=cap||!((input[0]>=L'A'&&input[0]<=L'Z')||(input[0]>=L'a'&&input[0]<=L'z'))||input[1]!=L':'||(input[2]!=L'/'&&input[2]!=L'\\'))return 0;
 for(size_t i=0;i<=n;i++)out[i]=input[i]==L'\\'?L'/':input[i];
 while(n>3&&out[n-1]==L'/')out[--n]=0;
 for(size_t start=3;start<n;){
  size_t end=start;while(end<n&&out[end]!=L'/')end++;
  if(end==start||out[end-1]==L'.'||out[end-1]==L' ')goto invalid;
  wchar_t base[9];size_t count=0;
  for(size_t i=start;i<end;i++){
   if(out[i]<32||wcschr(L"<>:\"|?*",out[i]))goto invalid;
   if(out[i]==L'.')break;
   if(count<8)base[count++]=towupper(out[i]);
  }
  base[count]=0;
  if(!wcscmp(base,L"CON")||!wcscmp(base,L"PRN")||!wcscmp(base,L"AUX")||!wcscmp(base,L"NUL")||
     (count==4&&(!wcsncmp(base,L"COM",3)||!wcsncmp(base,L"LPT",3))&&base[3]>=L'1'&&base[3]<=L'9'))goto invalid;
  /* Inspect the whole component as well as its device-name prefix. */
  for(size_t i=start;i<end;i++)if(out[i]<32||wcschr(L"<>:\"|?*",out[i]))goto invalid;
  start=end+1;
 }
 return 1;
invalid:
 out[0]=0;return 0;
}
static int topspin_portable_path(wchar_t *out,unsigned cap,const wchar_t *root,
 const wchar_t *override,const wchar_t *leaf,int portable)
{
 wchar_t normalized_root[32768];
 if(!out||!cap)return 0;
 out[0]=0;
 if(!topspin_portable_normalize(normalized_root,32768,root))return 0;
 size_t r=wcslen(normalized_root);
 if(override&&override[0]){
  if(!topspin_portable_normalize(out,cap,override))return 0;
  if(!portable)return 1;
  size_t n=wcslen(out);
  if(n<=r)goto invalid;
  for(size_t i=0;i<r;i++)if(towlower(out[i])!=towlower(normalized_root[i]))goto invalid;
  if(normalized_root[r-1]!=L'/'&&out[r]!=L'/')goto invalid;
  return 1;
 }
 if(!leaf||!leaf[0])return 0;
 size_t l=wcslen(leaf),separator=normalized_root[r-1]!=L'/';
 if(r+separator+l>=cap)return 0;
 wmemcpy(out,normalized_root,r);if(separator)out[r++]=L'/';wcscpy(out+r,leaf);
 /* Defaults are trusted leaf literals, but still validate aliases. */
 if(!topspin_portable_normalize(normalized_root,32768,out))goto invalid;
 return 1;
invalid:
 out[0]=0;return 0;
}
#endif
