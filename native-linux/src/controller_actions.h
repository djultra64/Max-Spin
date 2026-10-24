/* Logical actions are independent of host labels/positions. djultra64. */
#ifndef TOPSPIN_CONTROLLER_ACTIONS_H
#define TOPSPIN_CONTROLLER_ACTIONS_H
#include "input/input_identity.h"
#include "input/input_map.h"
typedef enum {TOPSPIN_CONTEXT_MENU,TOPSPIN_CONTEXT_MATCH} TopSpinControlContext;
typedef enum {
 TOPSPIN_ACTION_CONFIRM,TOPSPIN_ACTION_CANCEL,TOPSPIN_ACTION_ALTERNATE,TOPSPIN_ACTION_HELP,
 TOPSPIN_ACTION_NORMAL_SHOT,TOPSPIN_ACTION_TOPSPIN,TOPSPIN_ACTION_SLICE,TOPSPIN_ACTION_LOB,
 TOPSPIN_ACTION_PAUSE,TOPSPIN_ACTION_BACK,TOPSPIN_ACTION_COUNT
} TopSpinAction;
typedef struct {uint8_t source[TOPSPIN_ACTION_COUNT];InputBindings keys;} TopSpinActionBindings;
typedef struct {int source,face_label,family;char label[48];} TopSpinActionPrompt;
void topspin_actions_defaults(TopSpinActionBindings *bindings);
int topspin_action_bind(TopSpinActionBindings*,TopSpinAction,const char *physical_source);
int topspin_action_bind_key(TopSpinActionBindings*,TopSpinAction,const char *keys);
int topspin_action_in_context(TopSpinAction,TopSpinControlContext);
int topspin_actions_project(const TopSpinActionBindings*,TopSpinControlContext,InputPadMap*,InputBindings*);
int topspin_action_prompt(const TopSpinActionBindings*,TopSpinAction,const InputDeviceIdentity*,TopSpinActionPrompt*);
const char *topspin_action_name(TopSpinAction);
int topspin_family_parse(const char *name);
#endif
