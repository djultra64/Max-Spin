/* Frozen session preparation shared by native and portable CPU tests. djultra64. */
#include "character_launcher.h"
#include "character_launcher_platform.h"
#include "character_windows.h"
#include <stdio.h>
#include <string.h>

#define PATH_CAP 32768
static wchar_t owned[PATH_CAP];
static int join(wchar_t *out,const wchar_t *a,const wchar_t *b)
{
 int n=swprintf(out,PATH_CAP,L"%ls/%ls",a,b);
 return n>0&&n<PATH_CAP;
}
static int scrub(void)
{
 int ok=1;
 ok&=topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_SESSION",NULL);
 ok&=topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_MANIFEST_SHA256",NULL);
 ok&=topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_ASSETS",NULL);
 return ok;
}
static int fail(wchar_t *message,unsigned cap,const wchar_t *text)
{
 if(message&&cap){wcsncpy(message,text,cap-1);message[cap-1]=0;}
 return 0;
}
void topspin_windows_characters_end(const wchar_t *session)
{
 wchar_t candidate[PATH_CAP];
 if(owned[0]&&(!session||!join(candidate,session,L"characters")||wcscmp(candidate,owned)))return;
 topspin_windows_character_session_close();
 (void)scrub();
 if(owned[0]){topspin_character_platform_remove(owned);owned[0]=0;}
}
int topspin_windows_characters_begin(const wchar_t *install,const wchar_t *assets,
 const wchar_t *packages,const wchar_t *session,wchar_t *message,unsigned cap)
{
 wchar_t output[PATH_CAP],helper[PATH_CAP],python[PATH_CAP],wide_digest[65];
 char digest[65];int use_py=0;
 if(message&&cap)message[0]=0;
 topspin_windows_character_session_close();
 if(!scrub())return fail(message,cap,L"Cannot clear inherited athlete session settings.");
 if(!install||!assets||!packages||!session||!message||!cap)
  return fail(message,cap,L"Invalid athlete session paths.");
 if(owned[0])return fail(message,cap,L"An athlete session is already active.");
 int selected=topspin_character_platform_packages(packages);
 if(selected<0)return fail(message,cap,L"Cannot inspect the characters folder safely.");
 if(!selected)return 1;
 if(!join(output,session,L"characters")||!join(helper,install,L"native-linux/tools/character_session.py"))
  return fail(message,cap,L"Athlete session paths are too long.");
 if(topspin_character_platform_kind(output)!=0)
  return fail(message,cap,L"The athlete session output already exists or is unsafe.");
 if(topspin_character_platform_kind(helper)!=1)
  return fail(message,cap,L"Install the complete Max Spin package with its athlete preparation tools.");
 if(!topspin_character_platform_python(python,PATH_CAP,&use_py))
  return fail(message,cap,L"Python 3 is required for athlete packages. Install Python 3 or set MAX_SPIN_PYTHON to its executable.");
 wcscpy(owned,output);
 const wchar_t *args[10];unsigned count=0;
 args[count++]=python;if(use_py)args[count++]=L"-3";
 args[count++]=helper;args[count++]=L"--assets";args[count++]=assets;
 args[count++]=L"--characters";args[count++]=packages;args[count++]=L"--output";args[count++]=output;
 if(!topspin_character_platform_run(python,count,args,install,message,cap)){
  topspin_windows_characters_end(session);
  if(!message[0])fail(message,cap,L"Athlete preparation failed. Check Python and your character packages.");
  return 0;
 }
 if(!topspin_windows_character_session_validate(assets,output,digest)){
  topspin_windows_characters_end(session);
  return fail(message,cap,L"Cannot authenticate the prepared athlete session. Original files and replacements must match.");
 }
 for(unsigned i=0;i<64;i++)wide_digest[i]=(wchar_t)(unsigned char)digest[i];
 wide_digest[64]=0;
 if(!topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_SESSION",output)||
    !topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_MANIFEST_SHA256",wide_digest)||
    !topspin_character_platform_environment(L"MAX_SPIN_CHARACTER_ASSETS",assets)){
  topspin_windows_characters_end(session);
  return fail(message,cap,L"Cannot activate the prepared athlete session.");
 }
 return 1;
}
