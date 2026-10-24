/* Pure action bindings shared by report projection and prompt resolution.
 * This does not render or replace title UI resources. Own work: djultra64. */
#include "controller_actions.h"
#include <stdio.h>
#include <string.h>
static const char*names[]={"confirm","cancel","alternate","help","normal_shot","topspin","slice","lob","pause","back"};
static const int controls[]={INPUT_A,INPUT_B,INPUT_X,INPUT_Y,INPUT_A,INPUT_B,INPUT_X,INPUT_Y,INPUT_START,INPUT_BACK};
const char*topspin_action_name(TopSpinAction a){return(unsigned)a<TOPSPIN_ACTION_COUNT?names[a]:NULL;}
void topspin_actions_defaults(TopSpinActionBindings*b){
 memset(b,0,sizeof(*b));
 static const uint8_t sources[]={INPUT_SRC_SOUTH,INPUT_SRC_EAST,INPUT_SRC_WEST,INPUT_SRC_NORTH,INPUT_SRC_SOUTH,INPUT_SRC_EAST,INPUT_SRC_WEST,INPUT_SRC_NORTH,INPUT_SRC_START,INPUT_SRC_BACK};
 static const char*keys[]={"Space","Escape","U","I","Space","Escape","U","I","Return","Tab"};
 memcpy(b->source,sources,sizeof(sources));for(unsigned a=0;a<TOPSPIN_ACTION_COUNT;a++)input_bindings_parse(&b->keys,a,keys[a]);
}
int topspin_action_bind(TopSpinActionBindings*b,TopSpinAction a,const char*name){
 if(!b||!name||(unsigned)a>=TOPSPIN_ACTION_COUNT)return 0;
 int source=input_source_from_name(name);if(source<0)return 0;b->source[a]=(uint8_t)source;return 1;
}
int topspin_action_bind_key(TopSpinActionBindings*b,TopSpinAction a,const char*keys){
 if(!b||!keys||(unsigned)a>=TOPSPIN_ACTION_COUNT)return 0;
 InputBindings next=b->keys;if(input_bindings_parse(&next,a,keys))return 0;
 /* Accept only keys the actual SDL-to-VK consumer delivers. A name/glyph
  * must never promise a binding which the window silently ignores. */
 for(unsigned k=0;k<next.count[a];k++){
  unsigned code=next.key[a][k];
  if(!((code>='A'&&code<='Z')||(code>='0'&&code<='9')||(code>=0x70&&code<=0x7b)||
       code==0x09||code==0x0d||code==0x20||code==0x1b||code==0xa0||code==0xa1||(code>=0x25&&code<=0x28)))return 0;
 }
 b->keys=next;return 1;
}
int topspin_action_in_context(TopSpinAction a,TopSpinControlContext c){
 return (unsigned)a<TOPSPIN_ACTION_COUNT&&(c==TOPSPIN_CONTEXT_MENU||c==TOPSPIN_CONTEXT_MATCH)&&
        (a>=8||((a>=4)==(c==TOPSPIN_CONTEXT_MATCH)));
}
int topspin_actions_project(const TopSpinActionBindings*b,TopSpinControlContext c,InputPadMap*pad,InputBindings*keys){
 if(!b||!pad||!keys||(c!=TOPSPIN_CONTEXT_MENU&&c!=TOPSPIN_CONTEXT_MATCH))return 0;
 /* Validate a complete binding batch before modifying the output map. Two
  * simultaneously active actions cannot share one physical source. */
 for(unsigned a=0;a<TOPSPIN_ACTION_COUNT;a++)if(topspin_action_in_context((TopSpinAction)a,c)){
  if(b->source[a]>=INPUT_SRC_COUNT)return 0;
  for(unsigned j=0;j<a;j++)if(topspin_action_in_context((TopSpinAction)j,c)&&b->source[a]!=INPUT_SRC_NONE&&b->source[a]==b->source[j])return 0;
 }
 for(unsigned a=0;a<TOPSPIN_ACTION_COUNT;a++){
  if(!topspin_action_in_context((TopSpinAction)a,c))continue;
  int control=controls[a];pad->source[control]=b->source[a];keys->count[control]=b->keys.count[a];memcpy(keys->key[control],b->keys.key[a],sizeof(keys->key[control]));
 }return 1;
}
int topspin_family_parse(const char*n){
 if(!n||!strcmp(n,"auto"))return INPUT_FAMILY_AUTO;
 if(!strcmp(n,"xbox"))return INPUT_FAMILY_XBOX;
 if(!strcmp(n,"playstation"))return INPUT_FAMILY_PLAYSTATION;
 if(!strcmp(n,"nintendo")||!strcmp(n,"switch"))return INPUT_FAMILY_NINTENDO;
 return -2;
}
int topspin_action_prompt(const TopSpinActionBindings*b,TopSpinAction a,const InputDeviceIdentity*i,TopSpinActionPrompt*p){
 if(!b||!i||!p||(unsigned)a>=TOPSPIN_ACTION_COUNT)return 0;
 memset(p,0,sizeof(*p));p->source=b->source[a];p->family=i->family;
 if(i->kind==INPUT_DEVICE_KEYBOARD){
  if(!b->keys.count[a])snprintf(p->label,sizeof(p->label),"Unbound");
  else {const char*key=input_key_name(b->keys.key[a][0]);snprintf(p->label,sizeof(p->label),"%s",key?key:"Unknown key");}
  return 1;
 }
 if(p->source>=INPUT_SRC_SOUTH&&p->source<=INPUT_SRC_NORTH){
  unsigned position=p->source-INPUT_SRC_SOUTH;
  p->face_label=i->face_label[position];
  if(!p->face_label)p->face_label=input_family_face_label(i->family,position);
  static const char*labels[]={"","A","B","X","Y","Cross","Circle","Square","Triangle"};
  if(p->face_label>INPUT_LABEL_UNKNOWN&&p->face_label<=INPUT_LABEL_TRIANGLE){snprintf(p->label,sizeof(p->label),"%s",labels[p->face_label]);return 1;}
 }
 const char*name=input_source_name(p->source);snprintf(p->label,sizeof(p->label),"%s",p->source==INPUT_SRC_NONE?"Unbound":name?name:"Unknown control");return 1;
}
