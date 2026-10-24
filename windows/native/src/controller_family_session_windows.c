/* Native prompt family preference; keeps logical actions separate. djultra64. */
#include "controller_family_session.h"
#include "controller_actions.h"
#include "graphics_settings_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int topspin_windows_read_file(const char*,char*,size_t,size_t*);
static const char*token(int f){switch(f){case INPUT_FAMILY_AUTO:return "auto";case INPUT_FAMILY_XBOX:return "xbox";case INPUT_FAMILY_PLAYSTATION:return "playstation";case INPUT_FAMILY_NINTENDO:return "nintendo";default:return NULL;}}
static int fail(char*e,size_t n,const char*s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
int topspin_controller_family_load(int*out,char*e,size_t n){if(!out)return fail(e,n,"Missing controller preference");const char*p=getenv("TOPSPIN_CONTROLLER_SESSION_FILE");int family;if(p){char text[32];size_t used;if(!topspin_windows_read_file(p,text,16,&used)||used<2||text[used-1]!='\n'||memchr(text,0,used))return fail(e,n,"Invalid controller session file");text[used-1]=0;family=topspin_family_parse(text);if(!token(family)||strcmp(token(family),text))return fail(e,n,"Invalid controller preference");}else family=topspin_family_parse(getenv("TOPSPIN_CONTROLLER_FAMILY"));if(!token(family))return fail(e,n,"Invalid controller family");*out=family;if(e&&n)e[0]=0;return 1;}
int topspin_controller_family_change(int previous,int requested,int(*apply)(int),char*e,size_t n){if(!token(previous)||!token(requested)||!apply)return fail(e,n,"Invalid controller change");const char*p=getenv("TOPSPIN_CONTROLLER_SESSION_FILE");if(p){int current;if(!topspin_controller_family_load(&current,e,n))return 0;}if(!apply(requested))return fail(e,n,"Controller input unavailable");if(p){char text[32];snprintf(text,sizeof text,"%s\n",token(requested));if(!topspin_graphics_settings_store_file(p,text,e,n)){if(!apply(previous))return fail(e,n,"Controller preference rollback failed");return 0;}}if(e&&n)e[0]=0;return 1;}
